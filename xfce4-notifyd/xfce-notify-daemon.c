/*
 *  xfce4-notifyd
 *
 *  Copyright (c) 2008-2009 Brian Tarricone <bjt23@cornell.edu>
 *  Copyright (c) 2009 Jérôme Guelfucci <jeromeg@xfce.org>
 *
 *  The workarea per monitor code is taken from
 *  http://trac.galago-project.org/attachment/ticket/5/10-nd-improve-multihead-support.patch
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License ONLY.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <libxfce4util/libxfce4util.h>
#include <libxfcegui4/libxfcegui4.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <xfconf/xfconf.h>

#include "xfce-notify-daemon.h"
#include "xfce-notify-window.h"
#include "xfce-notify-marshal.h"

#define SPACE 16
#define XND_N_MONITORS xfce_notify_daemon_get_n_monitors_quark()

struct _XfceNotifyDaemon
{
    GObject parent;

    gint expire_timeout;
    gboolean fade_transparency;
    gdouble initial_opacity;
    GtkCornerType notify_location;

    DBusGConnection *dbus_conn;
    XfconfChannel *settings;

    GTree *active_notifications;
    GList ***reserved_rectangles;

    gint changed_screen;

    guint32 last_notification_id;
};

typedef struct
{
    GObjectClass parent;
} XfceNotifyDaemonClass;

enum
{
    SIG_NOTIFICATION_CLOSED = 0,
    SIG_ACTION_INVOKED,
    N_SIGS,
};

enum
{
    URGENCY_LOW = 0,
    URGENCY_NORMAL,
    URGENCY_CRITICAL,
};

static void xfce_notify_daemon_screen_changed(GdkScreen *screen,
                                              gpointer userdata);
static void xfce_notify_daemon_update_reserved_rectangles(gpointer key,
                                                          gpointer value,
                                                          gpointer data);
static void xfce_notify_daemon_finalize(GObject *obj);

static GQuark xfce_notify_daemon_get_n_monitors_quark(void);

static void xfce_gdk_rectangle_largest_box(GdkRectangle *src1,
                                           GdkRectangle *src2,
                                           GdkRectangle *dest);
static gboolean xfce_notify_daemon_get_workarea(GdkScreen *screen,
                                                guint monitor,
                                                GdkRectangle *rect);
static gboolean notify_get_capabilities(XfceNotifyDaemon *xndaemon,
                                        gchar ***OUT_capabilities,
                                        GError *error);
static gboolean notify_notify(XfceNotifyDaemon *xndaemon,
                              const gchar *app_name,
                              guint replaces_id,
                              const gchar *app_icon,
                              const gchar *summary,
                              const gchar *body,
                              const gchar **actions,
                              GHashTable *hints,
                              gint expire_timeout,
                              guint *OUT_id,
                              GError **error);
static gboolean notify_close_notification(XfceNotifyDaemon *xndaemon,
                                          guint id,
                                          GError **error);
static gboolean notify_get_server_information(XfceNotifyDaemon *xndaemon,
                                              gchar **OUT_name,
                                              gchar **OUT_vendor,
                                              gchar **OUT_version,
#ifndef USE_OLD_GET_SERVER_INFORMATION_SIGNATURE
                                              gchar **OUT_spec_version,
#endif
                                              GError **error);

static gboolean notify_quit(XfceNotifyDaemon *xndaemon,
                            GError **error);

static GdkPixbuf *notify_pixbuf_from_image_data(const GValue *image_data);

#include "notify-dbus.h"

static guint signals[N_SIGS] = { 0, };


G_DEFINE_TYPE(XfceNotifyDaemon, xfce_notify_daemon, G_TYPE_OBJECT)


static void
xfce_notify_daemon_class_init(XfceNotifyDaemonClass *klass)
{
    GObjectClass *gobject_class = (GObjectClass *)klass;

    gobject_class->finalize = xfce_notify_daemon_finalize;

    signals[SIG_NOTIFICATION_CLOSED] = g_signal_new("notification-closed",
                                                    XFCE_TYPE_NOTIFY_DAEMON,
                                                    G_SIGNAL_RUN_LAST,
                                                    0,
                                                    NULL, NULL,
#ifdef USE_OLD_NOTIFICATION_CLOSED_SIGNATURE
                                                    g_cclosure_marshal_VOID__UINT,
                                                    G_TYPE_NONE, 1,
                                                    G_TYPE_UINT
#else  /* libnotify 0.4.5 adds support */
                                                    xfce_notify_marshal_VOID__UINT_UINT,
                                                    G_TYPE_NONE, 2,
                                                    G_TYPE_UINT,
                                                    G_TYPE_UINT
#endif
                                                    );

    signals[SIG_ACTION_INVOKED] = g_signal_new("action-invoked",
                                               XFCE_TYPE_NOTIFY_DAEMON,
                                               G_SIGNAL_RUN_LAST,
                                               0,
                                               NULL, NULL,
                                               xfce_notify_marshal_VOID__UINT_STRING,
                                               G_TYPE_NONE, 2,
                                               G_TYPE_UINT,
                                               G_TYPE_STRING);

    dbus_g_object_type_install_info(G_TYPE_FROM_CLASS(klass),
                                    &dbus_glib_notify_object_info);
}

static gint
xfce_direct_compare(gconstpointer a,
                    gconstpointer b,
                    gpointer user_data)
{
    return (gint)((gchar *)a - (gchar *)b);
}

static GQuark
xfce_notify_daemon_get_n_monitors_quark(void)
{
    static GQuark quark = 0;

    if(!quark)
        quark = g_quark_from_static_string("xnd-n-monitors");

    return quark;
}

static void
xfce_notify_daemon_screen_changed(GdkScreen *screen, gpointer userdata)
{
    XfceNotifyDaemon *xndaemon = XFCE_NOTIFY_DAEMON(userdata);
    gint j;
    gint new_nmonitor = gdk_screen_get_n_monitors(screen);
    gint screen_number = gdk_screen_get_number(screen);
    gint old_nmonitor = GPOINTER_TO_INT(g_object_get_qdata(G_OBJECT(screen), XND_N_MONITORS));

    /* Set the new number of monitors */
    g_object_set_qdata(G_OBJECT(screen), XND_N_MONITORS, GINT_TO_POINTER(new_nmonitor));

    /* Free the current reserved rectangles on screen */
    for(j = 0; j < old_nmonitor; j++)
        g_list_free(xndaemon->reserved_rectangles[screen_number][j]);

    g_free(xndaemon->reserved_rectangles[screen_number]);

    /* Initialize a new reserved rectangles array for screen */
    xndaemon->reserved_rectangles[screen_number] = g_new0(GList *, new_nmonitor);
    xndaemon->changed_screen = screen_number;

    /* Traverse the active notifications tree to fill the new reserved rectangles array for screen */
    g_tree_foreach(xndaemon->active_notifications,
                   (GTraverseFunc)xfce_notify_daemon_update_reserved_rectangles,
                   xndaemon);
}

static void
xfce_notify_daemon_init(XfceNotifyDaemon *xndaemon)
{
    gint nscreen = gdk_display_get_n_screens(gdk_display_get_default());
    gint i;

    xndaemon->active_notifications = g_tree_new_full(xfce_direct_compare,
                                                   NULL, NULL,
                                                   (GDestroyNotify)gtk_widget_destroy);
    xndaemon->reserved_rectangles = g_new(GList **, nscreen);

    for(i = 0; i < nscreen; ++i) {
        GdkScreen *screen = gdk_display_get_screen(gdk_display_get_default(), i);
        gint nmonitor = gdk_screen_get_n_monitors(screen);

        g_object_set_qdata(G_OBJECT(screen), XND_N_MONITORS, GINT_TO_POINTER(nmonitor));

        g_signal_connect(G_OBJECT(screen), "monitors-changed",
                         G_CALLBACK(xfce_notify_daemon_screen_changed), xndaemon);

        xndaemon->reserved_rectangles[i] = g_new0(GList *, nmonitor);
    }

    xndaemon->last_notification_id = 1;
}

static void
xfce_notify_daemon_finalize(GObject *obj)
{
    gint nscreen = gdk_display_get_n_screens(gdk_display_get_default());
    gint i, j;
    XfceNotifyDaemon *xndaemon = XFCE_NOTIFY_DAEMON(obj);

    for(i = 0; i < nscreen; ++i) {
        GdkScreen *screen = gdk_display_get_screen(gdk_display_get_default(), i);
        gint nmonitor = gdk_screen_get_n_monitors(screen);

        for(j = 0; j < nmonitor; j++) {
            if (xndaemon->reserved_rectangles[i][j])
                g_list_free(xndaemon->reserved_rectangles[i][j]);
        }

        g_free(xndaemon->reserved_rectangles[i]);
    }

    g_free(xndaemon->reserved_rectangles);

    g_tree_destroy(xndaemon->active_notifications);

    if(xndaemon->settings)
        g_object_unref(xndaemon->settings);

    if(xndaemon->dbus_conn)
        dbus_g_connection_unref(xndaemon->dbus_conn);

    G_OBJECT_CLASS(xfce_notify_daemon_parent_class)->finalize(obj);
}



static guint32
xfce_notify_daemon_generate_id(XfceNotifyDaemon *xndaemon)
{
    if(G_UNLIKELY(xndaemon->last_notification_id == 0))
        xndaemon->last_notification_id = 1;

    return xndaemon->last_notification_id++;
}

static void
xfce_notify_daemon_window_action_invoked(XfceNotifyWindow *window,
                                         const gchar *action,
                                         gpointer user_data)
{
    XfceNotifyDaemon *xndaemon = user_data;
    guint id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(window),
                                                  "--notify-id"));
    g_signal_emit(G_OBJECT(xndaemon), signals[SIG_ACTION_INVOKED], 0,
                  id, action);
}

static void
xfce_notify_daemon_window_closed(XfceNotifyWindow *window,
                                 XfceNotifyCloseReason reason,
                                 gpointer user_data)
{
    XfceNotifyDaemon *xndaemon = user_data;
    gpointer id_p = g_object_get_data(G_OBJECT(window), "--notify-id");
    GList *list;
    gint screen = xfce_notify_window_get_last_screen(window);
    gint monitor = xfce_notify_window_get_last_monitor(window);

    /* Remove the reserved rectangle from the list */
    list = xndaemon->reserved_rectangles[screen][monitor];
    list = g_list_remove(list, xfce_notify_window_get_geometry(window));
    xndaemon->reserved_rectangles[screen][monitor] = list;

    g_tree_remove(xndaemon->active_notifications, id_p);
#ifdef USE_OLD_NOTIFICATION_CLOSED_SIGNATURE
    g_signal_emit(G_OBJECT(xndaemon), signals[SIG_NOTIFICATION_CLOSED], 0,
                  GPOINTER_TO_UINT(id_p));
#else  /* added to libnotify 0.4.5 */
    g_signal_emit(G_OBJECT(xndaemon), signals[SIG_NOTIFICATION_CLOSED], 0,
                  GPOINTER_TO_UINT(id_p), (guint)reason);
#endif
}

/* Gets the largest rectangle in src1 which does not contain src2. */
static void
xfce_gdk_rectangle_largest_box(GdkRectangle *src1, GdkRectangle *src2, GdkRectangle *dest)
{
    gint top = MAX(src2->y, src1->y);
    gint left = MAX(src2->x, src1->x);
    gint bottom = MAX(src2->height, src1->height) -
                  MIN(src2->y + src2->height, src1->y + src1->height);
    gint right = MAX(src2->width, src1->width) -
               MIN(src2->x + src2->width, src1->x + src1->width);
    gint medium_h = MAX(top, bottom);
    gint medium_w = MAX(left ,right);

    if(medium_h >= medium_w) {
        /* Height is largest */
        if(top >= bottom) {
            dest->x = src1->x;
            dest->y = src1->y;
            dest->width = src1->width;
            dest->height = top + MIN(0, bottom);
        } else {
            dest->x = src1->x;
            dest->y = src2->y + src2->height;
            dest->width = src1->width;
            dest->height = bottom + MIN(0, top);
        }
    } else {
        /* Width is largest */
        if(left >= right) {
            dest->x = src1->x;
            dest->y = src1->y;
            dest->width = medium_w + MIN(0, right);
            dest->height = src1->height;
        } else {
            dest->x = src2->x + src2->width;
            dest->y = src1->y;
            dest->width = medium_w + MIN(0, left);
            dest->height = src1->height;
        }
    }
}

static inline void
translate_origin(GdkRectangle *src1, gint xoffset, gint yoffset)
{
    src1->x += xoffset;
    src1->y += yoffset;
}

/* Returns the workarea (largest non-panel/dock occupied rectangle) for a given
   monitor. */
static gboolean
xfce_notify_daemon_get_workarea(GdkScreen *screen,
                                guint monitor_num,
                                GdkRectangle *workarea)
{
    GList *windows_list, *l;
    gint monitor_xoff, monitor_yoff;

    /* Defaults */
    gdk_screen_get_monitor_geometry(screen, monitor_num, workarea);

    monitor_xoff = workarea->x;
    monitor_yoff = workarea->y;

    if(!workarea)
        return FALSE;

    windows_list = gdk_screen_get_window_stack(screen);

    for(l = g_list_first(windows_list); l != NULL; l = g_list_next(l)) {
        GdkWindow *window = l->data;

        if(gdk_window_get_type_hint(window) == GDK_WINDOW_TYPE_HINT_DOCK) {
            GdkRectangle window_geom, intersection;

            gdk_window_get_frame_extents(window, &window_geom);

            if(gdk_rectangle_intersect(workarea, &window_geom, &intersection)){
                translate_origin(workarea, -monitor_xoff, -monitor_yoff);
                translate_origin(&intersection, -monitor_xoff, -monitor_yoff);

                xfce_gdk_rectangle_largest_box(workarea, &intersection, workarea);

                translate_origin(workarea, monitor_xoff, monitor_yoff);
                translate_origin(&intersection, monitor_xoff, monitor_yoff);
            }
        }

        g_object_unref(window);
    }

    g_list_free(windows_list);

    return TRUE;
}

static void
xfce_notify_daemon_window_size_allocate(GtkWidget *widget,
                                        GtkAllocation *allocation,
                                        gpointer user_data)
{
    XfceNotifyDaemon *xndaemon = user_data;
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(widget);
    GdkScreen *screen = NULL;
    gint x, y, monitor, screen_n, max_width;
    GdkRectangle *geom_tmp, geom, initial, workarea, widget_geom;
    GList *list;
    gboolean found = FALSE;

    DBG("Size allocate called.");

    workarea.x = 0;
    workarea.y = 0;
    workarea.width = 0;
    workarea.height = 0;

    geom_tmp = xfce_notify_window_get_geometry(window);
    if(geom_tmp->width != 0 && geom_tmp->height != 0) {
        /* Notification has already been placed previously. Not sure if that
         * can happen. */
        GList *old_list;

        screen_n = xfce_notify_window_get_last_screen(window);
        monitor = xfce_notify_window_get_last_monitor(window);
        old_list = xndaemon->reserved_rectangles[screen_n][monitor];

        old_list = g_list_remove(old_list, xfce_notify_window_get_geometry(window));
        xndaemon->reserved_rectangles[screen_n][monitor] = old_list;
    }

    gdk_display_get_pointer(gdk_display_get_default(), &screen, &x, &y, NULL);
    monitor = gdk_screen_get_monitor_at_point(screen, x, y);
    gdk_screen_get_monitor_geometry(screen, monitor, &geom);
    screen_n = gdk_screen_get_number (screen);

    if(xfce_notify_daemon_get_workarea(screen, monitor, &workarea)) {
        DBG("Workarea: (%i,%i), width: %i, height:%i",
            workarea.x, workarea.y, workarea.width, workarea.height);
        geom.x = workarea.x;
        geom.y = workarea.y;
        geom.width = workarea.width;
        geom.height = workarea.height;
    }

    DBG("We are on the monitor %i, screen %i", monitor, screen_n);

    gtk_window_set_screen(GTK_WINDOW(widget), screen);

    /* Set initial geometry */
    initial.width = allocation->width;
    initial.height = allocation->height;

    switch(xndaemon->notify_location) {
        case GTK_CORNER_TOP_LEFT:
            initial.x = geom.x + SPACE;
            initial.y = geom.y + SPACE;
            break;
        case GTK_CORNER_BOTTOM_LEFT:
            initial.x = geom.x + SPACE;
            initial.y = geom.y + geom.height - allocation->height - SPACE;
            break;
        case GTK_CORNER_TOP_RIGHT:
            initial.x = geom.x + geom.width - allocation->width - SPACE;
            initial.y = geom.y + SPACE;
            break;
        case GTK_CORNER_BOTTOM_RIGHT:
            initial.x = geom.x + geom.width - allocation->width - SPACE;
            initial.y = geom.y + geom.height - allocation->height - SPACE;
            break;
        default:
            g_warning("Invalid notify location: %d", xndaemon->notify_location);
            return;
    }

    widget_geom.x = initial.x;
    widget_geom.y = initial.y;
    widget_geom.width = initial.width;
    widget_geom.height = initial.height;
    max_width = 0;

    /* Get the list of reserved places */
    list = xndaemon->reserved_rectangles[screen_n][monitor];

    if(!list) {
        /* If the list is empty, there are no displayed notifications */
        DBG("No notifications on this monitor");

        xfce_notify_window_set_geometry(XFCE_NOTIFY_WINDOW(widget), widget_geom);
        xfce_notify_window_set_last_monitor(XFCE_NOTIFY_WINDOW(widget), monitor);
        xfce_notify_window_set_last_screen(XFCE_NOTIFY_WINDOW(widget), screen_n);

        list = g_list_prepend(list, xfce_notify_window_get_geometry(XFCE_NOTIFY_WINDOW(widget)));
        xndaemon->reserved_rectangles[screen_n][monitor] = list;

        DBG("Notification position: x=%i y=%i", widget_geom.x, widget_geom.y);
        gtk_window_move(GTK_WINDOW(widget), widget_geom.x, widget_geom.y);
        return;
    } else {
        /* Else, we try to find the appropriate position on the monitor */
        while(!found) {
            gboolean overlaps = FALSE;
            GList *l = NULL;
            gint notification_y, notification_height;

            DBG("Test if the candidate overlaps one of the existing notifications.");

            for(l = g_list_first(list); l; l = l->next) {
                GdkRectangle *rectangle = l->data;

                DBG("Overlaps with (x=%i, y=%i) ?", rectangle->x, rectangle->y);

                overlaps =  overlaps || gdk_rectangle_intersect(rectangle, &widget_geom, NULL);

                if(overlaps) {
                    DBG("Yes");

                    if(rectangle->width > max_width)
                        max_width = rectangle->width;

                    notification_y = rectangle->y;
                    notification_height = rectangle->height;

                    break;
                } else
                    DBG("No");
            }

            if(!overlaps) {
                DBG("We found a correct position.");
                found = TRUE;
            } else {
                switch(xndaemon->notify_location) {
                    case GTK_CORNER_TOP_LEFT:
                        DBG("Try under the current candiadate position.");
                        widget_geom.y = notification_y + notification_height + SPACE;

                        if(widget_geom.y + widget_geom.height > geom.height + geom.y) {
                            DBG("We reached the bottom of the monitor");
                            widget_geom.y = geom.y + SPACE;
                            widget_geom.x = widget_geom.x + max_width + SPACE;
                            max_width = 0;

                            if(widget_geom.x + widget_geom.width > geom.width + geom.x) {
                                DBG("There was no free space.");
                                widget_geom.x = initial.x;
                                widget_geom.y = initial.y;
                                found = TRUE;
                            }
                        }
                        break;
                    case GTK_CORNER_BOTTOM_LEFT:
                        DBG("Try above the current candidate position");
                        widget_geom.y = notification_y - widget_geom.height - SPACE;

                        if(widget_geom.y < geom.y) {
                            DBG("We reached the top of the monitor");
                            widget_geom.y = geom.y + geom.height - widget_geom.height - SPACE;
                            widget_geom.x = widget_geom.x + max_width + SPACE;
                            max_width = 0;

                            if(widget_geom.x + widget_geom.width > geom.width + geom.x) {
                                DBG("There was no free space.");
                                widget_geom.x = initial.x;
                                widget_geom.y = initial.y;
                                found = TRUE;
                            }
                        }
                        break;
                    case GTK_CORNER_TOP_RIGHT:
                        DBG("Try under the current candidate position.");
                        widget_geom.y = notification_y + notification_height + SPACE;

                        if(widget_geom.y + widget_geom.height > geom.height + geom.y) {
                            DBG("We reached the bottom of the monitor");
                            widget_geom.y = geom.y + SPACE;
                            widget_geom.x = widget_geom.x - max_width - SPACE;
                            max_width = 0;

                            if(widget_geom.x < geom.x) {
                                DBG("There was no free space.");
                                widget_geom.x = initial.x;
                                widget_geom.y = initial.y;
                                found = TRUE;
                            }
                        }
                        break;
                    case GTK_CORNER_BOTTOM_RIGHT:
                        DBG("Try above the current candidate position");
                        widget_geom.y = notification_y - widget_geom.height - SPACE;

                        if(widget_geom.y < geom.y) {
                            DBG("We reached the top of the screen");
                            widget_geom.y = geom.y + geom.height - widget_geom.height - SPACE;
                            widget_geom.x = widget_geom.x - max_width - SPACE;
                            max_width = 0;

                            if(widget_geom.x < geom.x) {
                                DBG("There was no free space");
                                widget_geom.x = initial.x;
                                widget_geom.y = initial.y;
                                found = TRUE;
                            }
                        }
                        break;

                    default:
                        g_warning("Invalid notify location: %d", xndaemon->notify_location);
                        return;
                }
            }
        }
    }

    xfce_notify_window_set_geometry(XFCE_NOTIFY_WINDOW(widget), widget_geom);
    xfce_notify_window_set_last_monitor(XFCE_NOTIFY_WINDOW(widget), monitor);
    xfce_notify_window_set_last_screen(XFCE_NOTIFY_WINDOW(widget), screen_n);

    list = g_list_prepend(list, xfce_notify_window_get_geometry(XFCE_NOTIFY_WINDOW(widget)));
    xndaemon->reserved_rectangles[screen_n][monitor] = list;

    DBG("Move the notification to: x=%i, y=%i", widget_geom.x, widget_geom.y);
    gtk_window_move(GTK_WINDOW(widget), widget_geom.x, widget_geom.y);
}


static void
xfce_notify_daemon_update_reserved_rectangles(gpointer key,
                                              gpointer value,
                                              gpointer data)
{
    XfceNotifyDaemon *xndaemon = XFCE_NOTIFY_DAEMON(data);
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(value);
    gint width, height;
    GtkAllocation allocation;

    if(xfce_notify_window_get_last_screen(window) != xndaemon->changed_screen)
      return;

    /* Get the size of the notification */
    gtk_window_get_size(GTK_WINDOW(window), &width, &height);

    allocation.x = 0;
    allocation.y = 0;
    allocation.width = width;
    allocation.height = height;

    xfce_notify_daemon_window_size_allocate(GTK_WIDGET(window), &allocation, xndaemon);
}


static gboolean
notify_get_capabilities(XfceNotifyDaemon *xndaemon,
                        gchar ***OUT_capabilities,
                        GError *error)
{
    gint i = 0;

    *OUT_capabilities = g_new(gchar *, 7);
    (*OUT_capabilities)[i++] = g_strdup("actions");
    (*OUT_capabilities)[i++] = g_strdup("body");
    (*OUT_capabilities)[i++] = g_strdup("body-markup");
#ifdef HAVE_LIBSEXY
    (*OUT_capabilities)[i++] = g_strdup("body-hyperlinks");
#endif
    (*OUT_capabilities)[i++] = g_strdup("icon-static");
    (*OUT_capabilities)[i++] = g_strdup("x-canonical-private-icon-only");
    (*OUT_capabilities)[i++] = NULL;

    return TRUE;
}

static gboolean
notify_notify(XfceNotifyDaemon *xndaemon,
              const gchar *app_name,
              guint replaces_id,
              const gchar *app_icon,
              const gchar *summary,
              const gchar *body,
              const gchar **actions,
              GHashTable *hints,
              gint expire_timeout,
              guint *OUT_id,
              GError **error)
{
    XfceNotifyWindow *window;
    GdkPixbuf *pix;
    GValue *urgency_data, *value_data;

    if((urgency_data = g_hash_table_lookup(hints, "urgency"))
       && G_VALUE_HOLDS(urgency_data, G_TYPE_UCHAR)
       && g_value_get_uchar(urgency_data) == URGENCY_CRITICAL)
    {
        /* don't expire urgent notifications */
        expire_timeout = 0;
    }

    if(expire_timeout == -1)
        expire_timeout = xndaemon->expire_timeout;

    if(replaces_id
       && (window = g_tree_lookup(xndaemon->active_notifications,
                                  GUINT_TO_POINTER(replaces_id))))
    {
        xfce_notify_window_set_icon_name(window, app_icon);
        xfce_notify_window_set_summary(window, summary);
        xfce_notify_window_set_body(window, body);
        xfce_notify_window_set_actions(window, actions);
        xfce_notify_window_set_expire_timeout(window, expire_timeout);
        xfce_notify_window_set_fade_transparent(window,
                                                xndaemon->fade_transparency);
        xfce_notify_window_set_opacity(window, xndaemon->initial_opacity);

        *OUT_id = replaces_id;
    } else {
        window = XFCE_NOTIFY_WINDOW(xfce_notify_window_new_with_actions(summary, body,
                                                                        app_icon,
                                                                        expire_timeout,
                                                                        actions));
        xfce_notify_window_set_fade_transparent(window,
                                                xndaemon->fade_transparency);
        xfce_notify_window_set_opacity(window, xndaemon->initial_opacity);

        *OUT_id = xfce_notify_daemon_generate_id(xndaemon);
        g_object_set_data(G_OBJECT(window), "--notify-id",
                          GUINT_TO_POINTER(*OUT_id));

        g_tree_insert(xndaemon->active_notifications,
                      GUINT_TO_POINTER(*OUT_id), window);

        g_signal_connect(G_OBJECT(window), "action-invoked",
                         G_CALLBACK(xfce_notify_daemon_window_action_invoked),
                         xndaemon);
        g_signal_connect(G_OBJECT(window), "closed",
                         G_CALLBACK(xfce_notify_daemon_window_closed),
                         xndaemon);
        g_signal_connect(G_OBJECT(window), "size-allocate",
                         G_CALLBACK(xfce_notify_daemon_window_size_allocate),
                         xndaemon);

        gtk_widget_show(GTK_WIDGET(window));
    }

    if(!app_icon || !*app_icon) {
        GValue *image_data = g_hash_table_lookup(hints, "image_data");
        if(!image_data)
            image_data = g_hash_table_lookup(hints, "icon_data");
        if(image_data) {
            pix = notify_pixbuf_from_image_data(image_data);
            if(pix) {
                xfce_notify_window_set_icon_pixbuf(window, pix);
                g_object_unref(G_OBJECT(pix));
            }
        } else {
            GValue *desktop_id = g_hash_table_lookup(hints, "desktop_id");
            if(desktop_id) {
                gchar *resource = g_strdup_printf("applications%c%s.desktop",
                                                  G_DIR_SEPARATOR,
                                                  g_value_get_string(desktop_id));
                XfceRc *rcfile = xfce_rc_config_open(XFCE_RESOURCE_DATA,
                                                     resource, TRUE);
                if(rcfile) {
                    if(xfce_rc_has_group(rcfile, "Desktop Entry")) {
                        const gchar *icon_file;
                        xfce_rc_set_group(rcfile, "Desktop Entry");
                        icon_file = xfce_rc_read_entry(rcfile, "Icon", NULL);
                        if(icon_file) {
                            pix = xfce_themed_icon_load(icon_file, 32);
                            if(pix) {
                                xfce_notify_window_set_icon_pixbuf(window, pix);
                                g_object_unref(G_OBJECT(pix));
                            }
                        }
                    }
                    xfce_rc_close(rcfile);
                }
                g_free(resource);
            }
        }
    }

    if(g_hash_table_lookup(hints, "x-canonical-private-icon-only"))
        xfce_notify_window_set_icon_only(window, TRUE);
    else
        xfce_notify_window_set_icon_only(window, FALSE);

    if((value_data = g_hash_table_lookup(hints, "value"))
       && G_VALUE_HOLDS_INT(value_data))
    {
        xfce_notify_window_set_gauge_value(window, g_value_get_int(value_data));
    } else
        xfce_notify_window_unset_gauge_value(window);

    gtk_widget_realize(GTK_WIDGET(window));

    return TRUE;
}

static gboolean
notify_close_notification(XfceNotifyDaemon *xndaemon,
                          guint id,
                          GError **error)
{
    XfceNotifyWindow *window = g_tree_lookup(xndaemon->active_notifications,
                                             GUINT_TO_POINTER(id));

    if(window)
        xfce_notify_window_closed(window, XFCE_NOTIFY_CLOSE_REASON_CLIENT);

    return TRUE;
}

static gboolean
notify_get_server_information(XfceNotifyDaemon *xndaemon,
                              gchar **OUT_name,
                              gchar **OUT_vendor,
                              gchar **OUT_version,
#ifndef USE_OLD_GET_SERVER_INFORMATION_SIGNATURE
                              gchar **OUT_spec_version,
#endif
                              GError **error)
{
    *OUT_name = g_strdup("Xfce Notify Daemon");
    *OUT_vendor = g_strdup("Xfce");
    *OUT_version = g_strdup(VERSION);
#ifndef USE_OLD_GET_SERVER_INFORMATION_SIGNATURE
    *OUT_spec_version = g_strdup(NOTIFICATIONS_SPEC_VERSION);
#endif

    return TRUE;
}

static gboolean
notify_quit(XfceNotifyDaemon *xndaemon,
            GError **error)
{
    gint i, main_level = gtk_main_level();
    for(i = 0; i < main_level; ++i)
        gtk_main_quit();
    return TRUE;
}



static GdkPixbuf *
notify_pixbuf_from_image_data(const GValue *image_data)
{
    GdkPixbuf *pix = NULL;
    GType struct_gtype;
    gint32 width, height, rowstride, bits_per_sample, channels;
    gboolean has_alpha;
    GArray *pixel_array;
    gsize correct_len;

    struct_gtype = dbus_g_type_get_struct("GValueArray", G_TYPE_INT,
                                          G_TYPE_INT, G_TYPE_INT,
                                          G_TYPE_BOOLEAN, G_TYPE_INT,
                                          G_TYPE_INT,
                                          DBUS_TYPE_G_UCHAR_ARRAY,
                                          G_TYPE_INVALID);
    if(!G_VALUE_HOLDS(image_data, struct_gtype)) {
        g_message("Image data is not the correct type");
        return NULL;
    }

    if(!dbus_g_type_struct_get(image_data,
                               0, &width,
                               1, &height,
                               2, &rowstride,
                               3, &has_alpha,
                               4, &bits_per_sample,
                               5, &channels,
                               6, &pixel_array,
                               G_MAXUINT))
    {
        g_message("Unable to retrieve image data struct members");
        return NULL;
    }

    correct_len = (height - 1) * rowstride + width
                  * ((channels * bits_per_sample + 7) / 8);
    if(correct_len != pixel_array->len) {
        g_message("Pixel data length (%d) did not match expected value (%u)",
                  pixel_array->len, (guint)correct_len);
        return NULL;
    }

    pix = gdk_pixbuf_new_from_data(g_memdup(pixel_array->data,
                                            pixel_array->len),
                                   GDK_COLORSPACE_RGB, has_alpha,
                                   bits_per_sample, width, height,
                                   rowstride,
                                   (GdkPixbufDestroyNotify)g_free, NULL);
    return pix;
}

static void
xfce_notify_daemon_set_theme(XfceNotifyDaemon *xndaemon,
                             const gchar *theme)
{
    gchar *file, **files;

    /* old-style ~/.themes ... */
    file = g_build_filename(xfce_get_homedir(), ".themes", theme,
                            "xfce-notify-4.0", "gtkrc", NULL);
    if(g_file_test(file, G_FILE_TEST_EXISTS)) {
        gtk_rc_parse(file);
        g_free(file);
        return;
    }
    g_free(file);

    file = g_strconcat("themes/", theme, "/xfce-notify-4.0/gtkrc", NULL);
    files = xfce_resource_lookup_all(XFCE_RESOURCE_DATA, file);
    if(files[0])
        gtk_rc_parse(files[0]);

    g_free(file);
    g_strfreev(files);
}



static void
xfce_notify_daemon_settings_changed(XfconfChannel *channel,
                                    const gchar *property,
                                    const GValue *value,
                                    gpointer user_data)
{
    XfceNotifyDaemon *xndaemon = user_data;

    if(!strcmp(property, "/expire-timeout")) {
        xndaemon->expire_timeout = G_VALUE_TYPE(value)
                                 ? g_value_get_int(value) : -1;
        if(xndaemon->expire_timeout != -1)
            xndaemon->expire_timeout *= 1000;
    } else if(!strcmp(property, "/fade-transparency")) {
        xndaemon->fade_transparency = G_VALUE_TYPE(value)
                                    ? g_value_get_boolean(value) : TRUE;
    } else if(!strcmp(property, "/initial-opacity")) {
        xndaemon->initial_opacity = G_VALUE_TYPE(value)
                                  ? g_value_get_double(value) : 0.9;
    } else if(!strcmp(property, "/theme")) {
        xfce_notify_daemon_set_theme(xndaemon,
                                     G_VALUE_TYPE(value)
                                     ? g_value_get_string(value)
                                     : "Default");
    } else if(!strcmp(property, "/notify-location")) {
        xndaemon->notify_location = G_VALUE_TYPE(value)
                                  ? g_value_get_uint(value)
                                  : GTK_CORNER_TOP_RIGHT;
    }
}

static gboolean
xfce_notify_daemon_start(XfceNotifyDaemon *xndaemon,
                         GError **error)
{
    int ret;
    DBusError derror;

    xndaemon->dbus_conn = dbus_g_bus_get(DBUS_BUS_SESSION, error);
    if(G_UNLIKELY(!xndaemon->dbus_conn)) {
        if(error && !*error) {
            g_set_error(error, DBUS_GERROR, DBUS_GERROR_FAILED,
                        _("Unable to connect to D-Bus session bus"));
        }
        return FALSE;
    }

    dbus_error_init(&derror);
    ret = dbus_bus_request_name(dbus_g_connection_get_connection(xndaemon->dbus_conn),
                                "org.freedesktop.Notifications",
                                DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                &derror);
    if(DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret) {
        if(dbus_error_is_set(&derror)) {
            if(error)
                dbus_set_g_error(error, &derror);
            dbus_error_free(&derror);
        } else if(error) {
            g_set_error(error, DBUS_GERROR, DBUS_GERROR_FAILED,
                        _("Another notification xndaemon is already running"));
        }

        return FALSE;
    }

    dbus_g_connection_register_g_object(xndaemon->dbus_conn,
                                        "/org/freedesktop/Notifications",
                                        G_OBJECT(xndaemon));

    return TRUE;
}

static gboolean
xfce_notify_daemon_load_config(XfceNotifyDaemon *xndaemon,
                               GError **error)
{
    gchar *theme;

    xndaemon->settings = xfconf_channel_new("xfce4-notifyd");

    xndaemon->expire_timeout = xfconf_channel_get_int(xndaemon->settings,
                                                    "/expire-timeout",
                                                    -1);
    if(xndaemon->expire_timeout != -1)
        xndaemon->expire_timeout *= 1000;

    xndaemon->fade_transparency = xfconf_channel_get_bool(xndaemon->settings,
                                                        "/fade-transparency",
                                                        TRUE);
    xndaemon->initial_opacity = xfconf_channel_get_double(xndaemon->settings,
                                                        "/initial-opacity",
                                                        0.9);

    theme = xfconf_channel_get_string(xndaemon->settings,
                                      "/theme", "Default");
    xfce_notify_daemon_set_theme(xndaemon, theme);
    g_free(theme);

    xndaemon->notify_location = xfconf_channel_get_uint(xndaemon->settings,
                                                      "/notify-location",
                                                      GTK_CORNER_TOP_RIGHT);

    g_signal_connect(G_OBJECT(xndaemon->settings), "property-changed",
                     G_CALLBACK(xfce_notify_daemon_settings_changed),
                     xndaemon);

    return TRUE;
}





XfceNotifyDaemon *
xfce_notify_daemon_new_unique(GError **error)
{
    XfceNotifyDaemon *xndaemon = g_object_new(XFCE_TYPE_NOTIFY_DAEMON, NULL);

    if(!xfce_notify_daemon_start(xndaemon, error)
       || !xfce_notify_daemon_load_config(xndaemon, error))
    {
        g_object_unref(G_OBJECT(xndaemon));
        return NULL;
    }

    return xndaemon;
}

