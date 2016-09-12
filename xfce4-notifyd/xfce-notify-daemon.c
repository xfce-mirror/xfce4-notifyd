/* vi:set et ai sw=4 sts=4 ts=4: */
/*
 *  xfce4-notifyd
 *
 *  Copyright (c) 2008-2009 Brian Tarricone <bjt23@cornell.edu>
 *  Copyright (c) 2009 Jérôme Guelfucci <jeromeg@xfce.org>
 *  Copyright (c) 2015 Ali Abdallah    <ali@xfce.org>
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
#include <libxfce4ui/libxfce4ui.h>

#include <gdk/gdkx.h>
#include <gio/gio.h>

#include <xfconf/xfconf.h>

#include "xfce-notify-gbus.h"

#include "xfce-notify-daemon.h"
#include "xfce-notify-window.h"
#include "xfce-notify-marshal.h"

#define SPACE 16
#define XND_N_MONITORS xfce_notify_daemon_get_n_monitors_quark()

struct _XfceNotifyDaemon
{
    XfceNotifyGBusSkeleton parent;

    XfceNotifyOrgXfceNotifyd *xfce_iface_skeleton;
    gint expire_timeout;
    guint bus_name_id;
    gdouble initial_opacity;
    GtkCornerType notify_location;
    gboolean do_fadeout;

    GtkCssProvider *css_provider;
    gboolean is_default_theme;

    XfconfChannel *settings;

    GTree *active_notifications;
    GList ***reserved_rectangles;
    GdkRectangle **monitors_workarea;

    gint changed_screen;

    guint close_timeout;

    guint32 last_notification_id;
};

typedef struct
{
    XfceNotifyGBusSkeletonClass  parent;

} XfceNotifyDaemonClass;


enum
{
    URGENCY_LOW = 0,
    URGENCY_NORMAL,
    URGENCY_CRITICAL,
};

static void xfce_notify_daemon_screen_changed(GdkScreen *screen,
                                              gpointer user_data);
static void xfce_notify_daemon_update_reserved_rectangles(gpointer key,
                                                          gpointer value,
                                                          gpointer data);
static void xfce_notify_daemon_finalize(GObject *obj);
static void  xfce_notify_daemon_constructed(GObject *obj);

static GQuark xfce_notify_daemon_get_n_monitors_quark(void);

static GdkFilterReturn xfce_notify_rootwin_watch_workarea(GdkXEvent *gxevent,
                                                          GdkEvent *event,
                                                          gpointer user_data);

static void xfce_gdk_rectangle_largest_box(GdkRectangle *src1,
                                           GdkRectangle *src2,
                                           GdkRectangle *dest);
static void xfce_notify_daemon_get_workarea(GdkScreen *screen,
                                            guint monitor,
                                            GdkRectangle *rect);
static void daemon_quit (XfceNotifyDaemon *xndaemon);
static gboolean xfce_notify_daemon_close_timeout(gpointer data);


static GdkPixbuf *notify_pixbuf_from_image_data(GVariant *image_data);

/* DBus method callbacks  forward declarations */
static gboolean notify_get_capabilities (XfceNotifyGBus *skeleton,
                                		 GDBusMethodInvocation   *invocation,
                                         XfceNotifyDaemon *xndaemon);


static gboolean notify_notify (XfceNotifyGBus *skeleton,
                               GDBusMethodInvocation   *invocation,
                               const gchar *app_name,
                               guint replaces_id,
                               const gchar *app_icon,
                               const gchar *summary,
                               const gchar *body,
                               const gchar **actions,
                               GVariant *hints,
                               gint expire_timeout,
                               XfceNotifyDaemon *xndaemon);


static gboolean notify_close_notification (XfceNotifyGBus *skeleton,
                                           GDBusMethodInvocation   *invocation,
                                           guint id,
                                           XfceNotifyDaemon *xndaemon);


static gboolean notify_get_server_information (XfceNotifyGBus *skeleton,
                                               GDBusMethodInvocation *invocation,
                                               XfceNotifyDaemon *xndaemon);


static gboolean notify_quit (XfceNotifyOrgXfceNotifyd *skeleton,
                             GDBusMethodInvocation   *invocation,
                             XfceNotifyDaemon *xndaemon);


G_DEFINE_TYPE(XfceNotifyDaemon, xfce_notify_daemon, XFCE_NOTIFY_TYPE_GBUS_SKELETON)


static void
xfce_notify_daemon_class_init(XfceNotifyDaemonClass *klass)
{
    GObjectClass *gobject_class = (GObjectClass *)klass;

    gobject_class->finalize = xfce_notify_daemon_finalize;
    gobject_class->constructed = xfce_notify_daemon_constructed;
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

static GdkFilterReturn
xfce_notify_rootwin_watch_workarea(GdkXEvent *gxevent,
                                   GdkEvent *event,
                                   gpointer user_data)
{
    XfceNotifyDaemon *xndaemon = XFCE_NOTIFY_DAEMON(user_data);
    XPropertyEvent *xevt = (XPropertyEvent *)gxevent;

    if(xevt->type == PropertyNotify
       && XInternAtom(xevt->display, "_NET_WORKAREA", False) == xevt->atom
       && xndaemon->monitors_workarea)
    {
        GdkScreen *screen = gdk_event_get_screen(event);
        int screen_number = gdk_screen_get_number (screen);
        int nmonitor = gdk_screen_get_n_monitors (screen);
        int j;

        DBG("got _NET_WORKAREA change on rootwin!");

        for(j = 0; j < nmonitor; j++)
            xfce_notify_daemon_get_workarea(screen, j,
                                            &(xndaemon->monitors_workarea[screen_number][j]));
    }

    return GDK_FILTER_CONTINUE;
}

static void
xfce_notify_daemon_screen_changed(GdkScreen *screen,
                                  gpointer user_data)
{
    XfceNotifyDaemon *xndaemon = XFCE_NOTIFY_DAEMON(user_data);
    gint j;
    gint new_nmonitor;
    gint screen_number;
    gint old_nmonitor;

    if(!xndaemon->monitors_workarea || !xndaemon->reserved_rectangles)
        /* Placement data not initialized, don't update it */
        return;

    new_nmonitor = gdk_screen_get_n_monitors(screen);
    screen_number = gdk_screen_get_number(screen);
    old_nmonitor = GPOINTER_TO_INT(g_object_get_qdata(G_OBJECT(screen), XND_N_MONITORS));

    DBG("Got 'screen-changed' signal for screen %d", screen_number);

    /* Set the new number of monitors */
    g_object_set_qdata(G_OBJECT(screen), XND_N_MONITORS, GINT_TO_POINTER(new_nmonitor));

    /* Free the current reserved rectangles on screen */
    for(j = 0; j < old_nmonitor; j++)
        g_list_free(xndaemon->reserved_rectangles[screen_number][j]);

    g_free(xndaemon->reserved_rectangles[screen_number]);
    g_free(xndaemon->monitors_workarea[screen_number]);

    xndaemon->monitors_workarea[screen_number] = g_new0(GdkRectangle, new_nmonitor);
    for(j = 0; j < new_nmonitor; j++) {
        DBG("Screen %d changed, updating workarea for monitor %d", screen_number, j);
        xfce_notify_daemon_get_workarea(screen, j,
                                        &(xndaemon->monitors_workarea[screen_number][j]));
    }

    /* Initialize a new reserved rectangles array for screen */
    xndaemon->reserved_rectangles[screen_number] = g_new0(GList *, new_nmonitor);
    xndaemon->changed_screen = screen_number;

    /* Traverse the active notifications tree to fill the new reserved rectangles array for screen */
    g_tree_foreach(xndaemon->active_notifications,
                   (GTraverseFunc)xfce_notify_daemon_update_reserved_rectangles,
                   xndaemon);
}

static void
xfce_notify_daemon_init_placement_data(XfceNotifyDaemon *xndaemon)
{
    gint nscreen = 1;
    gint i;

    xndaemon->reserved_rectangles = g_new(GList **, nscreen);
    xndaemon->monitors_workarea = g_new(GdkRectangle *, nscreen);

    for(i = 0; i < nscreen; ++i) {
        GdkScreen *screen = gdk_screen_get_default();
        gint nmonitor = gdk_screen_get_n_monitors(screen);
        GdkWindow *groot;
        int j;

        g_object_set_qdata(G_OBJECT(screen), XND_N_MONITORS, GINT_TO_POINTER(nmonitor));

        g_signal_connect(G_OBJECT(screen), "monitors-changed",
                         G_CALLBACK(xfce_notify_daemon_screen_changed), xndaemon);

        xndaemon->reserved_rectangles[i] = g_new0(GList *, nmonitor);
        xndaemon->monitors_workarea[i] = g_new0(GdkRectangle, nmonitor);

        for(j = 0; j < nmonitor; j++)
            xfce_notify_daemon_get_workarea(screen, j,
                                            &(xndaemon->monitors_workarea[i][j]));

        /* Monitor root window changes */
        groot = gdk_screen_get_root_window(screen);
        gdk_window_set_events(groot, gdk_window_get_events(groot) | GDK_PROPERTY_CHANGE_MASK);
        gdk_window_add_filter(groot, xfce_notify_rootwin_watch_workarea, xndaemon);
    }
}


static void
xfce_notify_bus_name_acquired_cb (GDBusConnection *connection,
                                  const gchar *name,
                                  gpointer user_data)
{
    XfceNotifyDaemon *xndaemon;
    GError *error = NULL;
    gboolean exported;


    xndaemon = XFCE_NOTIFY_DAEMON(user_data);

    exported =  g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (xndaemon),
                                                  connection,
                                                  "/org/freedesktop/Notifications",
                                                  &error);
    if (exported)
    {
        /* Connect dbus signals callbacks */
        g_signal_connect (xndaemon, "handle-notify",
                          G_CALLBACK(notify_notify), xndaemon);

        g_signal_connect (xndaemon, "handle-get-capabilities",
                          G_CALLBACK(notify_get_capabilities), xndaemon);

        g_signal_connect (xndaemon, "handle-get-server-information",
                          G_CALLBACK(notify_get_server_information), xndaemon);

        g_signal_connect (xndaemon, "handle-close-notification",
                          G_CALLBACK(notify_close_notification), xndaemon);
    }
    else
    {
        g_warning ("Failed to export interface: %s", error->message);
        g_error_free (error);
        gtk_main_quit ();
    }

    xndaemon->xfce_iface_skeleton  = xfce_notify_org_xfce_notifyd_skeleton_new();
    exported =  g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON(xndaemon->xfce_iface_skeleton),
                                                  connection,
                                                  "/org/freedesktop/Notifications",
                                                  &error);
    if (exported)
        g_signal_connect (xndaemon->xfce_iface_skeleton, "handle-quit",
                          G_CALLBACK(notify_quit), xndaemon);
    else
    {
        g_warning ("Failed to export interface: %s", error->message);
        g_error_free (error);
        gtk_main_quit ();
    }
}

static void
xfce_notify_bus_name_lost_cb (GDBusConnection *connection,
                              const gchar     *name,
                              gpointer         user_data)
{
    g_print (_("Another notification daemon is running, exiting\n"));
    daemon_quit(XFCE_NOTIFY_DAEMON(user_data));
}

static void xfce_notify_daemon_constructed (GObject *obj)
{
    XfceNotifyDaemon *self;

    self  = XFCE_NOTIFY_DAEMON (obj);

    self->bus_name_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                        "org.freedesktop.Notifications",
                                        G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                        xfce_notify_bus_name_acquired_cb,
                                        NULL,
                                        xfce_notify_bus_name_lost_cb,
                                        self,
                                        NULL);
}

static void
xfce_notify_daemon_init(XfceNotifyDaemon *xndaemon)
{
    xndaemon->active_notifications = g_tree_new_full(xfce_direct_compare,
                                                     NULL, NULL,
                                                     (GDestroyNotify)gtk_widget_destroy);

    xndaemon->last_notification_id = 1;
    xndaemon->reserved_rectangles = NULL;
    xndaemon->monitors_workarea = NULL;

    /* CSS Styling provider  */
    xndaemon->css_provider = gtk_css_provider_new ();

    xndaemon->close_timeout =
        g_timeout_add_seconds(600, (GSourceFunc) xfce_notify_daemon_close_timeout,
                              xndaemon);
}

static void
xfce_notify_daemon_finalize(GObject *obj)
{
    XfceNotifyDaemon *xndaemon = XFCE_NOTIFY_DAEMON(obj);
    GDBusConnection *connection;

    connection = g_dbus_interface_skeleton_get_connection(G_DBUS_INTERFACE_SKELETON(xndaemon));


    if ( g_dbus_interface_skeleton_has_connection(G_DBUS_INTERFACE_SKELETON(xndaemon),
                                                  connection))
    {
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON(xndaemon));
    }

    if (xndaemon->xfce_iface_skeleton &&
        g_dbus_interface_skeleton_has_connection(G_DBUS_INTERFACE_SKELETON(xndaemon->xfce_iface_skeleton),
                                                 connection))
    {
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON(xndaemon->xfce_iface_skeleton));
    }


    if(xndaemon->reserved_rectangles && xndaemon->monitors_workarea) {
      gint nscreen, i, j;

      nscreen = 1;

      for(i = 0; i < nscreen; ++i) {
          GdkScreen *screen = gdk_screen_get_default ();
          GdkWindow *groot = gdk_screen_get_root_window(screen);
          gint nmonitor = gdk_screen_get_n_monitors(screen);

          gdk_window_remove_filter(groot, xfce_notify_rootwin_watch_workarea, xndaemon);

          for(j = 0; j < nmonitor; j++) {
              if (xndaemon->reserved_rectangles[i][j])
                  g_list_free(xndaemon->reserved_rectangles[i][j]);
          }

          g_free(xndaemon->reserved_rectangles[i]);
          g_free(xndaemon->monitors_workarea[i]);
      }

      g_free(xndaemon->reserved_rectangles);
      g_free(xndaemon->monitors_workarea);
    }

    g_tree_destroy(xndaemon->active_notifications);

    g_object_unref (xndaemon->css_provider);

    if(xndaemon->settings)
        g_object_unref(xndaemon->settings);

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

    xfce_notify_gbus_emit_action_invoked (XFCE_NOTIFY_GBUS(xndaemon),
                                          id,
                                          action);
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
    if (g_tree_nnodes(xndaemon->active_notifications) == 0) {
        /* All notifications expired */
        /* Set a timeout to close xfce4-notifyd if it is idle
         * for 10 minutes */

        if(xndaemon->close_timeout)
            g_source_remove(xndaemon->close_timeout);

        xndaemon->close_timeout =
            g_timeout_add_seconds(600,
                                  (GSourceFunc) xfce_notify_daemon_close_timeout,
                                  xndaemon);
    }

    xfce_notify_gbus_emit_notification_closed (XFCE_NOTIFY_GBUS(xndaemon),
                                               GPOINTER_TO_UINT(id_p),
                                               (guint)reason);
}

/* Gets the largest rectangle in src1 which does not contain src2.
 * src2 is totally included in src1. */
/*
 *                    1
 *          ____________________
 *          |            ^      |
 *          |           d1      |
 *      4   |         ___|_     | 2
 *          | < d4  >|_____|<d2>|
 *          |            ^      |
 *          |___________d3______|
 *
 *                    3
 */
static void
xfce_gdk_rectangle_largest_box(GdkRectangle *src1,
                               GdkRectangle *src2,
                               GdkRectangle *dest)
{
    gint d1, d2, d3, d4; /* distance to the different sides of src1, see drawing above */
    gint max;

    d1 = src2->y - src1->y;
    d4 = src2->x - src1->x;
    d2 = src1->width - d4 - src2->width;
    d3 = src1->height - d1 - src2->height;

    /* Get the max of rectangles implied by d1, d2, d3 and d4 */
    max = MAX (d1 * src1->width, d2 * src1->height);
    max = MAX (max, d3 * src1->width);
    max = MAX (max, d4 * src1->height);

    if (max == d1 * src1->width) {
        dest->x = src1->x;
        dest->y = src1->y;
        dest->height = d1;
        dest->width = src1->width;
    }
    else if (max == d2 * src1->height) {
        dest->x = src2->x + src2->width;
        dest->y = src1->y;
        dest->width = d2;
        dest->height = src1->height;
    }
    else if (max == d3 * src1->width) {
        dest->x = src1->x;
        dest->y = src2->y + src2->height;
        dest->width = src1->width;
        dest->height = d3;
    }
    else {
        /* max == d4 * src1->height */
        dest->x = src1->x;
        dest->y = src1->y;
        dest->height = src1->height;
        dest->width = d4;
    }
}

static inline void
translate_origin(GdkRectangle *src1,
                 gint xoffset,
                 gint yoffset)
{
    src1->x += xoffset;
    src1->y += yoffset;
}

/* Returns the workarea (largest non-panel/dock occupied rectangle) for a given
   monitor. */
static void
xfce_notify_daemon_get_workarea(GdkScreen *screen,
                                guint monitor_num,
                                GdkRectangle *workarea)
{
    GdkDisplay *display;
    GList *windows_list, *l;
    gint monitor_xoff, monitor_yoff;

    DBG("Computing the workarea.");

    /* Defaults */
    gdk_screen_get_monitor_geometry(screen, monitor_num, workarea);

    monitor_xoff = workarea->x;
    monitor_yoff = workarea->y;

    /* Sync the display */
    display = gdk_screen_get_display(screen);
    gdk_display_sync(display);
    gdk_window_process_all_updates();

    windows_list = gdk_screen_get_window_stack(screen);

    if(!windows_list)
        DBG("No windows in stack.");

    for(l = g_list_first(windows_list); l != NULL; l = g_list_next(l)) {
        GdkWindow *window = l->data;
        GdkWindowTypeHint type_hint;

        gdk_error_trap_push();
        type_hint = gdk_window_get_type_hint(window);
        gdk_flush();

        if (gdk_error_trap_pop()) {
            DBG("Got invalid window in stack, could not get type hint");
            continue;
        }

        if(type_hint == GDK_WINDOW_TYPE_HINT_DOCK) {
            GdkRectangle window_geom, intersection;

            gdk_error_trap_push();
            gdk_window_get_frame_extents(window, &window_geom);
            gdk_flush();

            if (gdk_error_trap_pop()) {
                DBG("Got invalid window in stack, could not get frame extents");
                continue;
            }

            DBG("Got a dock window: x(%d), y(%d), w(%d), h(%d)",
                window_geom.x,
                window_geom.y,
                window_geom.width,
                window_geom.height);

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
}

static void
xfce_notify_daemon_window_size_allocate(GtkWidget *widget,
                                        GtkAllocation *allocation,
                                        gpointer user_data)
{
    XfceNotifyDaemon *xndaemon = user_data;
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(widget);
    GdkScreen *p_screen = NULL;
    GdkDevice *pointer;
#if GTK_CHECK_VERSION (3, 20, 0)
    GdkSeat *seat;
#else
    GdkScreen *widget_screen;
    GdkDisplay *display;
    GdkDeviceManager *device_manager;
#endif
    gint x, y, monitor, screen_n, max_width;
    GdkRectangle *geom_tmp, geom, initial, widget_geom;
    GList *list;
    gboolean found = FALSE;
    static gboolean placement_data_initialized = FALSE;

    DBG("Size allocate called for %d", xndaemon->last_notification_id);

    if (placement_data_initialized == FALSE) {
        /* First time we place a notification, initialize the arrays needed for
         * that (workarea, notification lists...). */
        xfce_notify_daemon_init_placement_data(xndaemon);
        placement_data_initialized = TRUE;
    }

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

#if GTK_CHECK_VERSION (3, 20, 0)
    seat = gdk_display_get_default_seat (gdk_display_get_default());
    pointer = gdk_seat_get_pointer (seat);
#else
    widget_screen = gtk_widget_get_screen (widget);
    display = gdk_screen_get_display (widget_screen);
    device_manager = gdk_display_get_device_manager (display);
    pointer = gdk_device_manager_get_client_pointer (device_manager);
#endif

    gdk_device_get_position (pointer, &p_screen, &x, &y);

    monitor = gdk_screen_get_monitor_at_point(p_screen, x, y);
    screen_n = gdk_screen_get_number (p_screen);

    DBG("We are on the monitor %i, screen %i", monitor, screen_n);

    geom = xndaemon->monitors_workarea[screen_n][monitor];

    DBG("Workarea: (%i,%i), width: %i, height:%i",
        geom.x, geom.y, geom.width, geom.height);

    gtk_window_set_screen(GTK_WINDOW(widget), p_screen);

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



static gboolean notify_get_capabilities (XfceNotifyGBus *skeleton,
                                         GDBusMethodInvocation   *invocation,
                                         XfceNotifyDaemon *xndaemon)
{
    const gchar *const capabilities[] =
    {
        "actions", "body", "body-hyperlinks", "body-markup", "icon-static",
        "x-canonical-private-icon-only", NULL
    };

    xfce_notify_gbus_complete_get_capabilities(skeleton, invocation, capabilities);

    if (g_tree_nnodes(xndaemon->active_notifications) == 0) {
        /* No active notifications, reset the close timeout */
        if(xndaemon->close_timeout)
            g_source_remove(xndaemon->close_timeout);

        xndaemon->close_timeout =
            g_timeout_add_seconds(600,
                                  (GSourceFunc) xfce_notify_daemon_close_timeout,
                                  xndaemon);
    }

    return TRUE;
}


static gboolean
notify_show_window (gpointer window)
{
    gtk_widget_show(GTK_WIDGET(window));
    return FALSE;
}

static void
add_and_propagate_css_provider (GtkWidget *widget, GtkStyleProvider *provider, guint priority)
{
    GList *children, *l;

    gtk_style_context_add_provider (gtk_widget_get_style_context (widget), provider, priority);

    if (GTK_IS_CONTAINER (widget))
    {
        children = gtk_container_get_children(GTK_CONTAINER(widget));
        for(l = children; l; l = l->next)
        {
            add_and_propagate_css_provider (GTK_WIDGET(l->data), provider, priority);
        }
        g_list_free (children);
    }
}

static void
notify_update_theme_for_window (XfceNotifyDaemon *xndaemon, GtkWidget *window, gboolean redraw)
{
    GtkStyleContext *context;

    context = gtk_widget_get_style_context (GTK_WIDGET(window));

    if (!xndaemon->is_default_theme)
    {
        if (gtk_style_context_has_class (context, "osd"))
            gtk_style_context_remove_class (context, "osd");
        if (gtk_style_context_has_class (context, "app-notification"))
            gtk_style_context_remove_class (context, "app-notification");

        add_and_propagate_css_provider (GTK_WIDGET(window),
                                        GTK_STYLE_PROVIDER(xndaemon->css_provider),
                                        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    else
    {
        /* These classes are normally defined in themes */
        if (!gtk_style_context_has_class (context, "osd"))
            gtk_style_context_add_class (context, "osd");

        if (!gtk_style_context_has_class (context, "app-notification"))
            gtk_style_context_add_class (context, "app-notification");

        /* Contains few style definition, use it as a fallback */
        add_and_propagate_css_provider (GTK_WIDGET(window),
                                        GTK_STYLE_PROVIDER(xndaemon->css_provider),
                                        GTK_STYLE_PROVIDER_PRIORITY_FALLBACK);
    }

    if (redraw)
    {
        gtk_widget_reset_style (window);
        gtk_widget_queue_draw (window);
    }
}

static gboolean
notify_update_theme_foreach (gpointer key, gpointer value, gpointer data)
{
    XfceNotifyDaemon *xndaemon = XFCE_NOTIFY_DAEMON(data);
    GtkWidget *window = GTK_WIDGET(value);

    notify_update_theme_for_window (xndaemon, window, TRUE);

    return FALSE;
}

static gboolean notify_notify (XfceNotifyGBus *skeleton,
                               GDBusMethodInvocation   *invocation,
                               const gchar *app_name,
                               guint replaces_id,
                               const gchar *app_icon,
                               const gchar *summary,
                               const gchar *body,
                               const gchar **actions,
                               GVariant *hints,
                               gint expire_timeout,
                               XfceNotifyDaemon *xndaemon)
{
    XfceNotifyWindow *window;
    GdkPixbuf *pix = NULL;
    GVariant *image_data = NULL;
    const gchar *desktop_id = NULL;
    gint value_hint = 0;
    gboolean value_hint_set = FALSE;
    gboolean x_canonical = FALSE;
    GVariant *item;
    GVariantIter iter;
    guint OUT_id;

    g_variant_iter_init (&iter, hints);

    while ((item = g_variant_iter_next_value (&iter)))
    {
        const char *key;
        GVariant   *value;

        g_variant_get (item,
                       "{sv}",
                       &key,
                   	   &value);

        if (g_strcmp0 (key, "urgency") == 0)
        {
            if (g_variant_is_of_type (value, G_VARIANT_TYPE_BYTE) &&
                (g_variant_get_byte(value) == URGENCY_CRITICAL))
            {
                /* don't expire urgent notifications */
                expire_timeout = 0;
            }
        }
        else if ((g_strcmp0 (key, "image_data") == 0) ||
                 (g_strcmp0 (key, "icon_data") == 0)  ||
                 (g_strcmp0 (key, "image-data") == 0) ||
                 (g_strcmp0 (key, "icon-data") == 0))
        {
            image_data = value;
        }
        else if ((g_strcmp0 (key, "desktop_entry") == 0) ||
                 (g_strcmp0 (key, "desktop-entry") == 0))
        {
            if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
                desktop_id = g_variant_get_string (value, NULL);
        }
        else if (g_strcmp0 (key, "value") == 0)
        {
            if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT32))
            {
                value_hint = g_variant_get_int32 (value);
                value_hint_set = TRUE;
            }
        }
        else if (g_strcmp0 (key, "x-canonical-private-icon-only") == 0)
            x_canonical = TRUE;

        g_variant_unref (item);
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
        xfce_notify_window_set_actions(window, actions, xndaemon->css_provider);
        xfce_notify_window_set_expire_timeout(window, expire_timeout);
        xfce_notify_window_set_opacity(window, xndaemon->initial_opacity);

        OUT_id = replaces_id;
    } else {
        window = XFCE_NOTIFY_WINDOW(xfce_notify_window_new_with_actions(summary, body,
                                                                        app_icon,
                                                                        expire_timeout,
                                                                        actions,
                                                                        xndaemon->css_provider));
        xfce_notify_window_set_opacity(window, xndaemon->initial_opacity);

        OUT_id = xfce_notify_daemon_generate_id(xndaemon);
        g_object_set_data(G_OBJECT(window), "--notify-id",
                          GUINT_TO_POINTER(OUT_id));

        g_tree_insert(xndaemon->active_notifications,
                      GUINT_TO_POINTER(OUT_id), window);

        g_signal_connect(G_OBJECT(window), "action-invoked",
                         G_CALLBACK(xfce_notify_daemon_window_action_invoked),
                         xndaemon);
        g_signal_connect(G_OBJECT(window), "closed",
                         G_CALLBACK(xfce_notify_daemon_window_closed),
                         xndaemon);
        g_signal_connect(G_OBJECT(window), "size-allocate",
                         G_CALLBACK(xfce_notify_daemon_window_size_allocate),
                         xndaemon);

        gtk_widget_realize(GTK_WIDGET(window));

        notify_update_theme_for_window (xndaemon, GTK_WIDGET(window), FALSE);

        g_idle_add(notify_show_window, window);
    }

    if(!app_icon || !*app_icon) {
        if(image_data) {
            pix = notify_pixbuf_from_image_data(image_data);
            if(pix) {
                xfce_notify_window_set_icon_pixbuf(window, pix);
                g_object_unref(G_OBJECT(pix));
            }
            g_variant_unref(image_data);
        } else {
            if(desktop_id) {
                gchar *resource = g_strdup_printf("applications%c%s.desktop",
                                                  G_DIR_SEPARATOR,
                                                  desktop_id);
                XfceRc *rcfile = xfce_rc_config_open(XFCE_RESOURCE_DATA,
                                                     resource, TRUE);
                if(rcfile) {
                    if(xfce_rc_has_group(rcfile, "Desktop Entry")) {
                        const gchar *icon_file;
                        xfce_rc_set_group(rcfile, "Desktop Entry");
                        icon_file = xfce_rc_read_entry(rcfile, "Icon", NULL);
                        if(icon_file) {
                            pix = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
                                                           icon_file,
                                                           32,
                                                           GTK_ICON_LOOKUP_FORCE_SIZE,
                                                           NULL);

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

    xfce_notify_window_set_icon_only(window, x_canonical);

    xfce_notify_window_set_do_fadeout(window, xndaemon->do_fadeout);

    if(value_hint_set)
        xfce_notify_window_set_gauge_value(window, value_hint, xndaemon->css_provider);
    else
        xfce_notify_window_unset_gauge_value(window);

    gtk_widget_realize(GTK_WIDGET(window));

    /* Remove close timeout as we display a new notification */
    if(xndaemon->close_timeout)
        g_source_remove(xndaemon->close_timeout);

    xndaemon->close_timeout = 0;
    xfce_notify_gbus_complete_notify(skeleton, invocation, OUT_id);

    return TRUE;
}


static gboolean notify_close_notification (XfceNotifyGBus *skeleton,
                                           GDBusMethodInvocation   *invocation,
                                           guint id,
                                           XfceNotifyDaemon *xndaemon)
{
    XfceNotifyWindow *window = g_tree_lookup(xndaemon->active_notifications,
                                             GUINT_TO_POINTER(id));

    if(window)
        xfce_notify_window_closed(window, XFCE_NOTIFY_CLOSE_REASON_CLIENT);

    xfce_notify_gbus_complete_close_notification(skeleton, invocation);

    return TRUE;
}

static gboolean notify_get_server_information (XfceNotifyGBus *skeleton,
                                               GDBusMethodInvocation   *invocation,
                                               XfceNotifyDaemon *xndaemon)
{
    xfce_notify_gbus_complete_get_server_information(skeleton,
                                                     invocation,
                                                     "Xfce Notify Daemon",
                                                     "Xfce",
                                                     VERSION,
                                                     NOTIFICATIONS_SPEC_VERSION);

    if (g_tree_nnodes(xndaemon->active_notifications) == 0) {
        /* No active notifications, reset the close timeout */
        if(xndaemon->close_timeout)
            g_source_remove(xndaemon->close_timeout);

        xndaemon->close_timeout =
            g_timeout_add_seconds(600,
                                  (GSourceFunc) xfce_notify_daemon_close_timeout,
                                  xndaemon);
    }

    return TRUE;
}


static void daemon_quit (XfceNotifyDaemon *xndaemon)
{
    gint i, main_level = gtk_main_level();
    for(i = 0; i < main_level; ++i)
        gtk_main_quit();
}


static gboolean notify_quit (XfceNotifyOrgXfceNotifyd *skeleton,
                             GDBusMethodInvocation   *invocation,
                             XfceNotifyDaemon *xndaemon)
{
    xfce_notify_org_xfce_notifyd_complete_quit (skeleton, invocation);
    daemon_quit(xndaemon);
    return TRUE;
}



static gboolean
xfce_notify_daemon_close_timeout(gpointer data)
{
    daemon_quit (XFCE_NOTIFY_DAEMON(data));
    return FALSE;
}


static GdkPixbuf *
notify_pixbuf_from_image_data(GVariant *image_data)
{
    GdkPixbuf *pix = NULL;
    gint32 width, height, rowstride, bits_per_sample, channels;
    gboolean has_alpha;
    GVariant *pixel_data;
    gsize correct_len;
    guchar *data;

    if (!g_variant_is_of_type (image_data, G_VARIANT_TYPE ("(iiibiiay)")))
    {
        g_warning ("Image data is not the correct type");
        return NULL;
    }

    g_variant_get (image_data,
                   "(iiibii@ay)",
                   &width,
                   &height,
                   &rowstride,
                   &has_alpha,
                   &bits_per_sample,
                   &channels,
                   &pixel_data);

    correct_len = (height - 1) * rowstride + width
                  * ((channels * bits_per_sample + 7) / 8);
    if(correct_len != g_variant_get_size (pixel_data)) {
        g_message ("Pixel data length (%lu) did not match expected value (%u)",
                   g_variant_get_size (pixel_data), (guint)correct_len);
        return NULL;
    }

    data = (guchar *) g_memdup (g_variant_get_data (pixel_data),
                                g_variant_get_size (pixel_data));

    pix = gdk_pixbuf_new_from_data(data,
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
    GError *error = NULL;
    gchar  *file, **files;
    gboolean css_parsed;

    DBG("New theme: %s", theme);

    file = g_build_filename(xfce_get_homedir(), ".themes", theme,
                            "xfce-notify-4.0", "gtk.css", NULL);

    xndaemon->is_default_theme = (g_strcmp0("Default", theme) == 0);

    if (!g_file_test(file, G_FILE_TEST_EXISTS)) {

        g_free (file);
        file = g_strconcat("themes/", theme, "/xfce-notify-4.0/gtk.css", NULL);
        files = xfce_resource_lookup_all(XFCE_RESOURCE_DATA, file);
        if (!files || !files[0])
        {
            g_warning ("theme '%s' is not found anywhere is user themes directories", theme);
            return;
        }
        file = g_strdup (files[0]);
        g_strfreev(files);
    }

    css_parsed =
        gtk_css_provider_load_from_path (xndaemon->css_provider,
                                         file,
                                         &error);
    if (!css_parsed)
    {
        g_warning ("Failed to parse css file: %s", error->message);
        g_error_free (error);
    }
    else
        g_tree_foreach (xndaemon->active_notifications,
                        (GTraverseFunc)notify_update_theme_foreach,
                        xndaemon);

    g_free(file);
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
    } else if(!strcmp(property, "/do-fadeout")) {
        xndaemon->do_fadeout = G_VALUE_TYPE(value)
                               ? g_value_get_boolean(value)
                               : TRUE;
    }
}


static gboolean
xfce_notify_daemon_load_config (XfceNotifyDaemon *xndaemon,
                               	GError **error)
{
    gchar *theme;

    xndaemon->settings = xfconf_channel_new("xfce4-notifyd");

    xndaemon->expire_timeout = xfconf_channel_get_int(xndaemon->settings,
                                                    "/expire-timeout",
                                                    -1);
    if(xndaemon->expire_timeout != -1)
        xndaemon->expire_timeout *= 1000;

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

    xndaemon->do_fadeout = xfconf_channel_get_bool(xndaemon->settings,
                                                "/do-fadeout", TRUE);

    g_signal_connect(G_OBJECT(xndaemon->settings), "property-changed",
                     G_CALLBACK(xfce_notify_daemon_settings_changed),
                     xndaemon);

    return TRUE;
}





XfceNotifyDaemon *
xfce_notify_daemon_new_unique(GError **error)
{
    XfceNotifyDaemon *xndaemon = g_object_new(XFCE_TYPE_NOTIFY_DAEMON, NULL);

    if(!xfce_notify_daemon_load_config(xndaemon, error))
    {
        g_object_unref(G_OBJECT(xndaemon));
        return NULL;
    }

    return xndaemon;
}
