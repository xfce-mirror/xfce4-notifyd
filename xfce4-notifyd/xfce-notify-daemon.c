/* vi:set et ai sw=4 sts=4 ts=4: */
/*
 *  xfce4-notifyd
 *
 *  Copyright (c) 2008-2023 Brian Tarricone <bjt23@cornell.edu>
 *  Copyright (c) 2009 Jérôme Guelfucci <jeromeg@xfce.org>
 *  Copyright (c) 2015 Ali Abdallah    <ali@xfce.org>
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

#include <gio/gio.h>

#ifdef ENABLE_X11
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#endif

#include <libxfce4util/libxfce4util.h>
#include <libxfce4ui/libxfce4ui.h>
#include <xfconf/xfconf.h>

#ifdef ENABLE_SOUND
#include <canberra-gtk.h>
#endif

#include <common/xfce-notify-common.h>
#include <common/xfce-notify-log-gbus.h>
#include <common/xfce-notify-log-util.h>

#include "xfce-notify-daemon.h"
#include "xfce-notify-daemon-log.h"
#include "xfce-notify-fdo-gbus.h"
#include "xfce-notify-gbus.h"
#include "xfce-notify-log.h"
#include "xfce-notify-window.h"
#include "xfce-notify-marshal.h"

#define SPACE 16
#define XND_N_MONITORS xfce_notify_daemon_get_n_monitors_quark()

struct _XfceNotifyDaemon
{
    XfceNotifyFdoGBusSkeleton parent;

    XfceNotifyGBus *xfce_iface_skeleton;
    XfceNotifyOldGBus *xfce_old_iface_skeleton;

    gboolean expire_timeout_enabled;
    gint expire_timeout;
    gboolean expire_timeout_allow_override;
    guint bus_name_id;
    guint notifyd_bus_name_id;
    gdouble initial_opacity;
    GtkCornerType notify_location;
    gboolean do_fadeout;
    gboolean do_slideout;
    gboolean do_not_disturb;
    gboolean notification_log;
    gboolean show_text_with_gauge;
    gint primary_monitor;
    gboolean windows_use_override_redirect;

    XfceNotifyDaemonLog *xndlog;
    XfceNotifyLog *log;
    gint log_level;
    gint log_level_apps;
    gboolean log_max_size_enabled;
    gint log_max_size;

    GtkCssProvider *css_provider;
    gboolean is_default_theme;

    XfconfChannel *settings;

    GTree *active_notifications;
    GList **reserved_rectangles;
    GdkRectangle *monitors_workarea;

    guint32 last_notification_id;

    GHashTable *denied_critical_notifications;
    GHashTable *excluded_from_log;

#ifdef ENABLE_SOUND
    gboolean mute_sounds;
#endif
};

typedef struct
{
    XfceNotifyFdoGBusSkeletonClass parent_class;
} XfceNotifyDaemonClass;


enum
{
    URGENCY_LOW = 0,
    URGENCY_NORMAL,
    URGENCY_CRITICAL,
};

// This deliberately leaves out '/theme', as that one is handled in
// a special way.
const struct {
    const gchar *name;
    GType type;
    gsize offset;
    union {
        gint i;
        guint u;
        gdouble d;
        gboolean b;
    } default_value;
} settings[] = {
    {
        .name = EXPIRE_TIMEOUT_ENABLED_PROP,
        .type = G_TYPE_BOOLEAN,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, expire_timeout_enabled),
        .default_value.b = TRUE,
    },
    {
        .name = EXPIRE_TIMEOUT_PROP,
        .type = G_TYPE_INT,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, expire_timeout),
        .default_value.i = EXPIRE_TIMEOUT_DEFAULT,
    },
    {
        .name = EXPIRE_TIMEOUT_ALLOW_OVERRIDE_PROP,
        .type = G_TYPE_BOOLEAN,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, expire_timeout_allow_override),
        .default_value.b = TRUE,
    },
    {
        .name = "/initial-opacity",
        .type = G_TYPE_DOUBLE,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, initial_opacity),
        .default_value.d = 0.9,
    },
    {
        .name = "/notify-location",
        .type = G_TYPE_UINT,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, notify_location),
        .default_value.u = GTK_CORNER_TOP_RIGHT,
    },
    {
        .name = "/do-fadeout",
        .type = G_TYPE_BOOLEAN,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, do_fadeout),
        .default_value.b = TRUE,
    },
    {
        .name = "/do-slideout",
        .type = G_TYPE_BOOLEAN,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, do_slideout),
        .default_value.b = FALSE,
    },
    {
        .name = "/show-text-with-gauge",
        .type = G_TYPE_BOOLEAN,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, show_text_with_gauge),
        .default_value.b = FALSE,
    },
#ifdef ENABLE_SOUND
    {
        .name = MUTE_SOUNDS_PROP,
        .type = G_TYPE_BOOLEAN,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, mute_sounds),
        .default_value.b = FALSE,
    },
#endif
    {
        .name = "/primary-monitor",
        .type = G_TYPE_UINT,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, primary_monitor),
        .default_value.u = 0,
    },
    {
        .name = "/do-not-disturb",
        .type = G_TYPE_BOOLEAN,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, do_not_disturb),
        .default_value.b = FALSE,
    },
    {
        .name = "/notification-log",
        .type = G_TYPE_BOOLEAN,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, notification_log),
        .default_value.b = FALSE,
    },
    {
        .name = "/log-level",
        .type = G_TYPE_UINT,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, log_level),
        .default_value.u = 0,
    },
    {
        .name = "/log-level-apps",
        .type = G_TYPE_UINT,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, log_level_apps),
        .default_value.u = 0,
    },
    {
        .name = LOG_MAX_SIZE_ENABLED_PROP,
        .type = G_TYPE_BOOLEAN,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, log_max_size_enabled),
        .default_value.b = TRUE,
    },
    {
        .name = LOG_MAX_SIZE_PROP,
        .type = G_TYPE_UINT,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, log_max_size),
        .default_value.u = LOG_MAX_SIZE_DEFAULT,
    },
    {
        .name = COMPAT_OVERRIDE_REDIRECT_PROP,
        .type = G_TYPE_BOOLEAN,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, windows_use_override_redirect),
        .default_value.b = FALSE,
    },
};

static void xfce_notify_daemon_screen_changed(GdkScreen *screen,
                                              gpointer user_data);
static gboolean xfce_notify_daemon_update_reserved_rectangles(gpointer key,
                                                              gpointer value,
                                                              gpointer data);
static void xfce_notify_daemon_finalize(GObject *obj);
static void xfce_notify_daemon_constructed(GObject *obj);

static GQuark xfce_notify_daemon_get_n_monitors_quark(void);

#ifdef ENABLE_X11
static GdkFilterReturn xfce_notify_rootwin_watch_workarea(GdkXEvent *gxevent,
                                                          GdkEvent *event,
                                                          gpointer user_data);
#endif

static void daemon_quit (XfceNotifyDaemon *xndaemon);

/* DBus method callbacks  forward declarations */
static gboolean notify_get_capabilities(XfceNotifyFdoGBus *skeleton,
                                        GDBusMethodInvocation   *invocation,
                                        XfceNotifyDaemon *xndaemon);

static void notify_update_known_applications (XfconfChannel *channel,
                                              gchar *app_name);

static gboolean notify_application_is_muted (XfconfChannel *channel,
                                             gchar *new_app_name);

static gboolean notify_notify(XfceNotifyFdoGBus *skeleton,
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


static gboolean notify_close_notification(XfceNotifyFdoGBus *skeleton,
                                          GDBusMethodInvocation *invocation,
                                          guint id,
                                          XfceNotifyDaemon *xndaemon);


static gboolean notify_get_server_information(XfceNotifyFdoGBus *skeleton,
                                              GDBusMethodInvocation *invocation,
                                              XfceNotifyDaemon *xndaemon);


static gboolean xfce_notify_quit(XfceNotifyGBus *skeleton,
                                 GDBusMethodInvocation *invocation,
                                 XfceNotifyDaemon *xndaemon);

static gboolean xfce_notify_old_quit(XfceNotifyOldGBus *skeleton,
                                     GDBusMethodInvocation *invocation,
                                     XfceNotifyDaemon *xndaemon);


G_DEFINE_TYPE(XfceNotifyDaemon, xfce_notify_daemon, XFCE_TYPE_NOTIFY_FDO_GBUS_SKELETON)


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

static gint
xfce_notify_daemon_get_monitor_index (GdkDisplay *display,
                                      GdkMonitor *monitor)
{
    gint i, nmonitors;

    nmonitors = gdk_display_get_n_monitors (display);

    for (i = 0; i < nmonitors; i++) {
        if (monitor == gdk_display_get_monitor (display, i))
            return i;
    }

    return 0;
}

static void
xfce_notify_daemon_update_workarea_positions(XfceNotifyDaemon *xndaemon, gint old_nmonitor, gint new_nmonitor) {

    /* Free the current reserved rectangles on screen */
    for (gint j = 0; j < old_nmonitor; j++) {
        g_list_free(xndaemon->reserved_rectangles[j]);
        xndaemon->reserved_rectangles[j] = NULL;
    }

    if (old_nmonitor != new_nmonitor) {
        // Number of monitors have changed, so realloc the arrays
        g_free(xndaemon->reserved_rectangles);
        xndaemon->reserved_rectangles = g_new0(GList *, new_nmonitor);

        g_free(xndaemon->monitors_workarea);
        xndaemon->monitors_workarea = g_new0(GdkRectangle, new_nmonitor);
    }

    for (gint j = 0; j < new_nmonitor; j++) {
        GdkMonitor *monitor = gdk_display_get_monitor(gdk_display_get_default(), j);
        DBG("Screen changed, updating workarea for monitor %d", j);
        gdk_monitor_get_workarea(monitor, &(xndaemon->monitors_workarea[j]));
    }

    /* Traverse the active notifications tree to fill the new reserved rectangles array for screen */
    g_tree_foreach(xndaemon->active_notifications,
                   xfce_notify_daemon_update_reserved_rectangles,
                   xndaemon);
}

#ifdef ENABLE_X11
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
        GdkDisplay *display = gdk_screen_get_display(screen);
        int nmonitor = gdk_display_get_n_monitors(display);

        DBG("got _NET_WORKAREA change on rootwin!");
        xfce_notify_daemon_update_workarea_positions(xndaemon, nmonitor, nmonitor);
    }

    return GDK_FILTER_CONTINUE;
}
#endif

static void
xfce_notify_daemon_screen_changed(GdkScreen *screen,
                                  gpointer user_data)
{
    XfceNotifyDaemon *xndaemon = XFCE_NOTIFY_DAEMON(user_data);
    GdkDisplay *display = gdk_screen_get_display(screen);
    gint new_nmonitor;
    gint old_nmonitor;

    if(!xndaemon->monitors_workarea || !xndaemon->reserved_rectangles)
        /* Placement data not initialized, don't update it */
        return;

    new_nmonitor = gdk_display_get_n_monitors(display);
    old_nmonitor = GPOINTER_TO_INT(g_object_get_qdata(G_OBJECT(screen), XND_N_MONITORS));

    DBG("Got 'screen-changed' signal");

    /* Set the new number of monitors */
    g_object_set_qdata(G_OBJECT(screen), XND_N_MONITORS, GINT_TO_POINTER(new_nmonitor));

    xfce_notify_daemon_update_workarea_positions(xndaemon, old_nmonitor, new_nmonitor);
}

static void
xfce_notify_daemon_init_placement_data(XfceNotifyDaemon *xndaemon)
{
    GdkScreen *screen = gdk_screen_get_default();
    GdkDisplay *display = gdk_screen_get_display(screen);
    gint nmonitor = gdk_display_get_n_monitors(display);
    int j;

    g_object_set_qdata(G_OBJECT(screen), XND_N_MONITORS, GINT_TO_POINTER(nmonitor));

    g_signal_connect(G_OBJECT(screen), "monitors-changed",
                     G_CALLBACK(xfce_notify_daemon_screen_changed), xndaemon);

    xndaemon->reserved_rectangles = g_new0(GList *, nmonitor);
    xndaemon->monitors_workarea = g_new0(GdkRectangle, nmonitor);

    for(j = 0; j < nmonitor; j++) {
        GdkMonitor *monitor = gdk_display_get_monitor(display, j);
        gdk_monitor_get_workarea(monitor, &(xndaemon->monitors_workarea[j]));
    }

#ifdef ENABLE_X11
    if (GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
        /* Monitor root window changes */
        GdkWindow *groot = gdk_screen_get_root_window(screen);
        gdk_window_set_events(groot, gdk_window_get_events(groot) | GDK_PROPERTY_CHANGE_MASK);
        gdk_window_add_filter(groot, xfce_notify_rootwin_watch_workarea, xndaemon);
    }
#endif
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

    if (g_strcmp0(name, "org.freedesktop.Notifications") == 0) {
        exported =  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON (xndaemon),
                                                     connection,
                                                     "/org/freedesktop/Notifications",
                                                     &error);
        if (exported) {
            /* Connect dbus signals callbacks */
            g_signal_connect(xndaemon, "handle-notify",
                             G_CALLBACK(notify_notify), xndaemon);

            g_signal_connect(xndaemon, "handle-get-capabilities",
                             G_CALLBACK(notify_get_capabilities), xndaemon);

            g_signal_connect(xndaemon, "handle-get-server-information",
                             G_CALLBACK(notify_get_server_information), xndaemon);

            g_signal_connect(xndaemon, "handle-close-notification",
                             G_CALLBACK(notify_close_notification), xndaemon);
        } else {
            g_warning("Failed to export interface: %s", error->message);
            g_error_free(error);
            gtk_main_quit();
        }

        xndaemon->xfce_old_iface_skeleton  = xfce_notify_old_gbus_skeleton_new();
        exported = g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(xndaemon->xfce_old_iface_skeleton),
                                                    connection,
                                                    "/org/freedesktop/Notifications",
                                                    &error);
        if (exported) {
            g_signal_connect(xndaemon->xfce_old_iface_skeleton, "handle-quit",
                             G_CALLBACK(xfce_notify_old_quit), xndaemon);
        } else {
            g_warning("Failed to export interface: %s", error->message);
            g_error_free(error);
            gtk_main_quit();
        }
    } else if (g_strcmp0(name, "org.xfce.Notifyd") == 0) {
        xndaemon->xfce_iface_skeleton  = xfce_notify_gbus_skeleton_new();
        exported = g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(xndaemon->xfce_iface_skeleton),
                                                    connection,
                                                    "/org/xfce/Notifyd",
                                                    &error);
        if (exported) {
            g_signal_connect(xndaemon->xfce_iface_skeleton, "handle-quit",
                             G_CALLBACK(xfce_notify_quit), xndaemon);
        } else {
            g_warning("Failed to export interface: %s", error->message);
            g_error_free(error);
            gtk_main_quit();
        }

        xndaemon->xndlog = xfce_notify_daemon_log_new(connection, xndaemon->log, &error);
        if (xndaemon->xndlog == NULL) {
            g_warning("Failed to export interface: %s", error->message);
            g_error_free(error);
            gtk_main_quit();
        }
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

static void
xfce_notify_daemon_constructed(GObject *obj) {
    XfceNotifyDaemon *self = XFCE_NOTIFY_DAEMON (obj);

    self->bus_name_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                        "org.freedesktop.Notifications",
                                        G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                        xfce_notify_bus_name_acquired_cb,
                                        NULL,
                                        xfce_notify_bus_name_lost_cb,
                                        self,
                                        NULL);
    self->notifyd_bus_name_id = g_bus_own_name(G_BUS_TYPE_SESSION,
                                               "org.xfce.Notifyd",
                                               G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                               xfce_notify_bus_name_acquired_cb,
                                               NULL,
                                               xfce_notify_bus_name_lost_cb,
                                               self,
                                               NULL);
}

static void
log_row_added(XfceNotifyLog *log, const gchar *entry_id, XfceNotifyDaemon *xndaemon) {
    if (xndaemon->xndlog != NULL) {
        xfce_notify_log_gbus_emit_row_added(XFCE_NOTIFY_LOG_GBUS(xndaemon->xndlog), entry_id);
    }
}

static void
log_row_changed(XfceNotifyLog *log, const gchar *entry_id, XfceNotifyDaemon *xndaemon) {
    if (xndaemon->xndlog != NULL) {
        xfce_notify_log_gbus_emit_row_changed(XFCE_NOTIFY_LOG_GBUS(xndaemon->xndlog), entry_id != NULL ? entry_id : "");
    }
}

static void
log_row_deleted(XfceNotifyLog *log, const gchar *entry_id, XfceNotifyDaemon *xndaemon) {
    if (xndaemon->xndlog != NULL) {
        xfce_notify_log_gbus_emit_row_deleted(XFCE_NOTIFY_LOG_GBUS(xndaemon->xndlog), entry_id);
    }
}

static void
log_truncated(XfceNotifyLog *log, guint n_kept_entries, XfceNotifyDaemon *xndaemon) {
    if (xndaemon->xndlog != NULL) {
        xfce_notify_log_gbus_emit_truncated(XFCE_NOTIFY_LOG_GBUS(xndaemon->xndlog), n_kept_entries);
    }
}

static void
log_cleared(XfceNotifyLog *log, XfceNotifyDaemon *xndaemon) {
    if (xndaemon->xndlog != NULL) {
        xfce_notify_log_gbus_emit_cleared(XFCE_NOTIFY_LOG_GBUS(xndaemon->xndlog));
    }
}

static void
xfce_notify_daemon_init(XfceNotifyDaemon *xndaemon)
{
    GError *error = NULL;

    xndaemon->active_notifications = g_tree_new_full(xfce_direct_compare,
                                                     NULL, NULL,
                                                     (GDestroyNotify)gtk_widget_destroy);

    xndaemon->last_notification_id = 1;
    xndaemon->reserved_rectangles = NULL;
    xndaemon->monitors_workarea = NULL;

    /* CSS Styling provider  */
    xndaemon->css_provider = gtk_css_provider_new ();

    xndaemon->denied_critical_notifications = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    xndaemon->excluded_from_log = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    xndaemon->log = xfce_notify_log_open(&error);
    if (xndaemon->log == NULL) {
        g_warning("Unable to open notification log: %s", error != NULL ? error->message : "(uknknown error)");
        if (error != NULL) {
            g_error_free(error);
        }
    } else {
        g_signal_connect(xndaemon->log, "row-added",
                         G_CALLBACK(log_row_added), xndaemon);
        g_signal_connect(xndaemon->log, "row-changed",
                         G_CALLBACK(log_row_changed), xndaemon);
        g_signal_connect(xndaemon->log, "row-deleted",
                         G_CALLBACK(log_row_deleted), xndaemon);
        g_signal_connect(xndaemon->log, "truncated",
                         G_CALLBACK(log_truncated), xndaemon);
        g_signal_connect(xndaemon->log, "cleared",
                         G_CALLBACK(log_cleared), xndaemon);
    }
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

    if (xndaemon->xfce_old_iface_skeleton &&
        g_dbus_interface_skeleton_has_connection(G_DBUS_INTERFACE_SKELETON(xndaemon->xfce_old_iface_skeleton),
                                                 connection))
    {
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON(xndaemon->xfce_old_iface_skeleton));
    }

    if (xndaemon->xndlog != NULL) {
        g_object_unref(xndaemon->xndlog);
    }

    if(xndaemon->reserved_rectangles && xndaemon->monitors_workarea) {
      GdkScreen *screen = gdk_screen_get_default ();
      gint nmonitor =  gdk_display_get_n_monitors(gdk_screen_get_display(screen));
      gint i;

#ifdef ENABLE_X11
      if (GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
          GdkWindow *groot = gdk_screen_get_root_window(screen);
          gdk_window_remove_filter(groot, xfce_notify_rootwin_watch_workarea, xndaemon);
      }
#endif

      for(i = 0; i < nmonitor; i++) {
          if (xndaemon->reserved_rectangles[i])
              g_list_free(xndaemon->reserved_rectangles[i]);
      }

      g_free(xndaemon->reserved_rectangles);
      g_free(xndaemon->monitors_workarea);
    }

    g_tree_destroy(xndaemon->active_notifications);

    g_object_unref (xndaemon->css_provider);

    if(xndaemon->settings)
        g_object_unref(xndaemon->settings);

    g_hash_table_destroy(xndaemon->denied_critical_notifications);
    g_hash_table_destroy(xndaemon->excluded_from_log);

    if (xndaemon->log != NULL) {
        g_object_unref(xndaemon->log);
    }

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
    guint id = xfce_notify_window_get_id(window);

    if (G_LIKELY(id > 0)) {
        xfce_notify_fdo_gbus_emit_action_invoked(XFCE_NOTIFY_FDO_GBUS(xndaemon),
                                                 id,
                                                 action);
    } else {
        g_warning("Notify window somehow didn't have an ID");
    }
}

static void
xfce_notify_daemon_window_closed(XfceNotifyWindow *window,
                                 XfceNotifyCloseReason reason,
                                 gpointer user_data)
{
    XfceNotifyDaemon *xndaemon = user_data;
    guint id = xfce_notify_window_get_id(window);
    GList *list;
    gint monitor = xfce_notify_window_get_last_monitor(window);

    /* Remove the reserved rectangle from the list */
    list = xndaemon->reserved_rectangles[monitor];
    list = g_list_remove(list, xfce_notify_window_get_geometry(window));
    xndaemon->reserved_rectangles[monitor] = list;

    if (reason == XFCE_NOTIFY_CLOSE_REASON_DISMISSED && xndaemon->log != NULL) {
        const gchar *log_id = xfce_notify_window_get_log_id(window);

        if (log_id != NULL) {
            xfce_notify_log_mark_read(xndaemon->log, log_id);
        }
    }

    if (G_LIKELY(id > 0)) {
        g_tree_remove(xndaemon->active_notifications, GUINT_TO_POINTER(id));

        xfce_notify_fdo_gbus_emit_notification_closed(XFCE_NOTIFY_FDO_GBUS(xndaemon),
                                                      id,
                                                      (guint)reason);
    } else {
        g_warning("Notify window somehow didn't have an ID");
    }
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
    GdkScreen *widget_screen;
    GdkDisplay *display;
    GdkSeat *seat;
    GdkMonitor *monitor;
    gint x, y, monitor_num, old_monitor, max_width;
    GdkRectangle old_geom, geom, initial, widget_geom;
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

    old_geom = *xfce_notify_window_get_geometry(window);
    old_monitor = xfce_notify_window_get_last_monitor(window);

    widget_screen = gtk_widget_get_screen (widget);
    display = gdk_screen_get_display(widget_screen);
    seat = gdk_display_get_default_seat(display);
    pointer = gdk_seat_get_pointer (seat);

    gdk_device_get_position (pointer, &p_screen, &x, &y);

    if (xndaemon->primary_monitor == 1) {
        monitor = gdk_display_get_primary_monitor(display);
    } else {
        monitor = gdk_display_get_monitor_at_point(display, x, y);
    }
    monitor_num = xfce_notify_daemon_get_monitor_index(display, monitor);

    DBG("We are on the monitor %i", monitor_num);

    geom = xndaemon->monitors_workarea[monitor_num];

    DBG("Workarea: (%i,%i), width: %i, height:%i",
        geom.x, geom.y, geom.width, geom.height);

    if(old_geom.width != 0 && old_geom.height != 0) {
        /* Notification has already been placed previously. */
        xndaemon->reserved_rectangles[old_monitor] = g_list_remove(xndaemon->reserved_rectangles[old_monitor],
                                                                   xfce_notify_window_get_geometry(window));
    }

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
    list = xndaemon->reserved_rectangles[monitor_num];

    if(!list) {
        /* If the list is empty, there are no displayed notifications */
        DBG("No notifications on this monitor");

        xfce_notify_window_set_geometry(XFCE_NOTIFY_WINDOW(widget), widget_geom);
        xfce_notify_window_set_last_monitor(XFCE_NOTIFY_WINDOW(widget), monitor_num);

        list = g_list_prepend(list, xfce_notify_window_get_geometry(XFCE_NOTIFY_WINDOW(widget)));
        xndaemon->reserved_rectangles[monitor_num] = list;

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
    xfce_notify_window_set_last_monitor(XFCE_NOTIFY_WINDOW(widget), monitor_num);

    list = g_list_prepend(list, xfce_notify_window_get_geometry(XFCE_NOTIFY_WINDOW(widget)));
    xndaemon->reserved_rectangles[monitor_num] = list;

    DBG("Move the notification to: x=%i, y=%i", widget_geom.x, widget_geom.y);
    gtk_window_move(GTK_WINDOW(widget), widget_geom.x, widget_geom.y);
}


static gboolean
xfce_notify_daemon_update_reserved_rectangles(gpointer key,
                                              gpointer value,
                                              gpointer data)
{
    XfceNotifyDaemon *xndaemon = XFCE_NOTIFY_DAEMON(data);
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(value);
    gint width, height;
    GtkAllocation allocation;

    /* Get the size of the notification */
    gtk_window_get_size(GTK_WINDOW(window), &width, &height);

    allocation.x = 0;
    allocation.y = 0;
    allocation.width = width;
    allocation.height = height;

    xfce_notify_daemon_window_size_allocate(GTK_WIDGET(window), &allocation, xndaemon);

    return FALSE;
}



static gboolean notify_get_capabilities(XfceNotifyFdoGBus *skeleton,
                                        GDBusMethodInvocation *invocation,
                                        XfceNotifyDaemon *xndaemon)
{
    const gchar *const capabilities[] = {
        "action-icons",
        "actions",
        "body",
        "body-hyperlinks",
        "body-markup",
        "icon-static",
#ifdef ENABLE_SOUND
        "sound",
#endif
        "x-canonical-private-icon-only",
        NULL,
    };

    xfce_notify_fdo_gbus_complete_get_capabilities(skeleton, invocation, capabilities);

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
        if (gtk_style_context_has_class (context, "keycap"))
            gtk_style_context_remove_class (context, "keycap");

        add_and_propagate_css_provider (GTK_WIDGET(window),
                                        GTK_STYLE_PROVIDER(xndaemon->css_provider),
                                        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    else
    {
        /* These classes are normally defined in themes */
        if (!gtk_style_context_has_class (context, "keycap"))
            gtk_style_context_add_class (context, "keycap");

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

static void
notify_update_known_applications (XfconfChannel *channel, gchar *new_app_name)
{
    GPtrArray *known_applications;
    GValue *val;
    gint index = 0;

    val = g_new0 (GValue, 1);
    g_value_init (val, G_TYPE_STRING);
    g_value_take_string (val, new_app_name);

    known_applications = xfconf_channel_get_arrayv (channel, KNOWN_APPLICATIONS_PROP);
    /* No known applications, initialize the channel and property */
    if (known_applications == NULL || known_applications->len < 1) {
        GPtrArray *array;
        array = g_ptr_array_new ();
        g_ptr_array_add (array, val);
        if (!xfconf_channel_set_arrayv (channel, KNOWN_APPLICATIONS_PROP, array))
            g_warning ("Could not initialize the application log: %s", new_app_name);
        g_ptr_array_unref (array);
    }
    /* Add the new application to the list unless it's already known */
    else {
        guint i;
        gboolean application_is_known = FALSE;
        /* Check if the application is actually unknown */
        for (i = 0; i < known_applications->len; i++) {
            GValue *known_application;
            known_application = g_ptr_array_index (known_applications, i);
            /* Remember where to put the application in alphabetical order */
            if (g_ascii_strcasecmp (g_value_get_string (known_application), new_app_name) < 0)
                index = i + 1;
            /* Just to be sure that we've found the exact same application don't ignore the case when comparing strings here */
            else if (g_strcmp0 (new_app_name, g_value_get_string (known_application)) == 0) {
                application_is_known = TRUE;
                break;
            }
        }
        /* Unknown application, add it in alphabetical order */
        if (application_is_known == FALSE) {
            g_ptr_array_insert (known_applications, index, val);
            if (!xfconf_channel_set_arrayv (channel, KNOWN_APPLICATIONS_PROP, known_applications))
                g_warning ("Could not add a new application to the log: %s", new_app_name);
        }
        else {
            g_free (val);
        }
    }
    xfconf_array_free (known_applications);
}

static gboolean
notify_application_is_muted (XfconfChannel *channel, gchar *new_app_name)
{
    GPtrArray *muted_applications;
    guint i;

    muted_applications = xfconf_channel_get_arrayv (channel, MUTED_APPLICATIONS_PROP);

    /* Check whether this application should be muted */
    if (muted_applications != NULL) {
        for (i = 0; i < muted_applications->len; i++) {
            GValue *muted_application;
            muted_application = g_ptr_array_index (muted_applications, i);
            if (g_str_match_string (new_app_name, g_value_get_string (muted_application), FALSE) == TRUE) {
                return TRUE;
            }
        }
    }
    xfconf_array_free (muted_applications);
    return FALSE;
}

static gchar *
xfce_notify_log_insert(XfceNotifyLog *log,
                       gint log_max_size,
                       GDateTime *timestamp,
                       const gchar *app_id,
                       const gchar *summary,
                       const gchar *body,
                       GVariant *image_data,
                       const gchar *image_path,
                       const gchar *app_icon,
                       const gchar *desktop_id,
                       gint expire_timeout,
                       const gchar **actions)
{
    gchar *id = NULL;
    XfceNotifyLogEntry *entry = xfce_notify_log_entry_new_empty();
    entry->timestamp = g_date_time_ref(timestamp);
    entry->app_id = g_strdup(app_id);
    entry->icon_id = xfce_notify_log_cache_icon(image_data,
                                                image_path,
                                                app_icon,
                                                desktop_id);
    entry->summary = g_strdup(summary);
    entry->body = g_strdup(body);
    entry->expire_timeout = expire_timeout;
    entry->is_read = FALSE;

    for (gint i = 0; actions != NULL && actions[i] != NULL; i += 2) {
        XfceNotifyLogEntryAction *action = g_new0(XfceNotifyLogEntryAction, 1);
        action->id = g_strdup(actions[i]);
        action->label = g_strdup(actions[i+1]);
        entry->actions = g_list_prepend(entry->actions, action);
    }
    entry->actions = g_list_reverse(entry->actions);

    xfce_notify_log_write(log, entry);
    id = g_strdup(entry->id);
    xfce_notify_log_entry_unref(entry);

    if (log_max_size > 0) {
        xfce_notify_log_truncate(log, log_max_size);
    }

    return id;
}

static gboolean
notify_notify(XfceNotifyFdoGBus *skeleton,
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
    GDateTime *timestamp;
    GdkPixbuf *pix = NULL;
    GVariant *image_data = NULL;
    GVariant *icon_data = NULL;
    const gchar *image_path = NULL;
    gchar *desktop_id = NULL;
    gchar *new_app_name;
    gint value_hint = 0;
    gint urgency = URGENCY_NORMAL;
    gboolean value_hint_set = FALSE;
    gboolean x_canonical = FALSE;
    gboolean transient = FALSE;
    gboolean actions_are_icon_names = FALSE;
#ifdef ENABLE_SOUND
    gboolean has_sound = FALSE;
    ca_proplist *sound_props = NULL;
    int ca_err;
#endif
    GVariant *item;
    GVariantIter iter;
    guint OUT_id = replaces_id != 0 ? replaces_id : xfce_notify_daemon_generate_id(xndaemon);
    gboolean application_is_muted = FALSE;

#ifdef ENABLE_SOUND
    if (!xndaemon->mute_sounds) {
        ca_err = ca_proplist_create(&sound_props);
        if (ca_err != CA_SUCCESS) {
            g_message("Failed to create sound property list: %s", ca_strerror(ca_err));
        }
    }
#endif

    g_variant_iter_init (&iter, hints);

    while ((item = g_variant_iter_next_value (&iter)))
    {
        gchar *key;
        GVariant   *value;

        g_variant_get (item,
                       "{sv}",
                       &key,
                   	   &value);

        if (g_strcmp0 (key, "urgency") == 0)
        {
            if (g_variant_is_of_type (value, G_VARIANT_TYPE_BYTE)) {
                urgency = g_variant_get_byte(value);
            }
            g_variant_unref(value);
        }
        else if ((g_strcmp0 (key, "image-data") == 0) ||
                 (g_strcmp0 (key, "image_data") == 0))
        {
            if (image_data) {
                g_variant_unref(image_data);
            }
            image_data = value;
        }
        else if ((g_strcmp0 (key, "icon-data") == 0) ||
                 (g_strcmp0 (key, "icon_data") == 0))
        {
            if (icon_data) {
                g_variant_unref(icon_data);
            }
            icon_data = value;
        }
        else if ((g_strcmp0 (key, "image-path") == 0) ||
                 (g_strcmp0 (key, "image_path") == 0))
        {
            image_path = g_variant_get_string (value, NULL);
            g_variant_unref(value);
        }
        else if ((g_strcmp0 (key, "desktop_entry") == 0) ||
                 (g_strcmp0 (key, "desktop-entry") == 0))
        {
            if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
                desktop_id = g_variant_dup_string (value, NULL);

            g_variant_unref(value);
        }
        else if (g_strcmp0 (key, "value") == 0)
        {
            if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT32))
            {
                value_hint = g_variant_get_int32 (value);
                value_hint_set = TRUE;
            }
            g_variant_unref(value);
        }
        else if (g_strcmp0 (key, "transient") == 0)
        {
            transient = TRUE;
            g_variant_unref(value);
        } else if (g_strcmp0(key, "action-icons") == 0) {
            if (g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
                actions_are_icon_names = g_variant_get_boolean(value);
            }
            g_variant_unref(value);
        } else if (g_strcmp0 (key, "x-canonical-private-icon-only") == 0)
        {
            x_canonical = TRUE;
            g_variant_unref(value);
        }
#ifdef ENABLE_SOUND
        else if (g_strcmp0(key, "sound-file") == 0) {
            if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
                if (sound_props != NULL) {
                    ca_proplist_sets(sound_props, CA_PROP_MEDIA_FILENAME, g_variant_get_string(value, NULL));
                    has_sound = TRUE;
                }
            }
            g_variant_unref(value);
        } else if (g_strcmp0(key, "sound-name") == 0) {
            if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
                if (sound_props != NULL) {
                    ca_proplist_sets(sound_props, CA_PROP_EVENT_ID, g_variant_get_string(value, NULL));
                    has_sound = TRUE;
                }
            }
            g_variant_unref(value);
        } else if (g_strcmp0(key, "suppress-sound") == 0) {
            if (g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
                if (sound_props != NULL && g_variant_get_boolean(value)) {
                    ca_proplist_destroy(sound_props);
                    sound_props = NULL;
                }
            }
            g_variant_unref(value);
        }
#endif
        else
        {
            g_variant_unref(value);
        }

        g_free(key);
        g_variant_unref (item);
    }

    if (desktop_id)
        new_app_name = g_strdup (desktop_id);
    else
        new_app_name = g_strdup (app_name);

    if (urgency == URGENCY_CRITICAL && g_hash_table_contains(xndaemon->denied_critical_notifications, new_app_name)) {
        urgency = URGENCY_NORMAL;
    }
    if (urgency == URGENCY_CRITICAL) {
        /* don't expire urgent notifications */
        expire_timeout = 0;
    }

    notify_update_known_applications (xndaemon->settings, new_app_name);

    if (expire_timeout == -1 || !xndaemon->expire_timeout_allow_override) {
        expire_timeout = xndaemon->expire_timeout_enabled ? xndaemon->expire_timeout : 0;
    }

    application_is_muted = notify_application_is_muted (xndaemon->settings, new_app_name);

#ifdef ENABLE_SOUND
    if (sound_props != NULL && !has_sound) {
        ca_proplist_destroy(sound_props);
        sound_props = NULL;
    }
#endif

    timestamp = g_date_time_new_now_local();

    /* Don't show notification bubbles in the "Do not disturb" mode or if the
       application has been muted by the user. Exceptions are "urgent"
       notifications. */
    if (urgency != URGENCY_CRITICAL) {
        if (xndaemon->do_not_disturb == TRUE ||
            application_is_muted == TRUE)
        {
            /* Notifications marked as transient will never be logged */
            if (xndaemon->notification_log == TRUE
                && transient == FALSE
                && !g_hash_table_contains(xndaemon->excluded_from_log, new_app_name))
            {
                /* Either log in DND mode or always for muted apps */
                if ((xndaemon->log_level == 0 && xndaemon->do_not_disturb == TRUE) ||
                    xndaemon->log_level == 1)
                      /* Log either all, all except muted or only muted applications */
                      if (xndaemon->log != NULL
                          && (xndaemon->log_level_apps == 0
                              || (xndaemon->log_level_apps == 1 && application_is_muted == FALSE)
                              || (xndaemon->log_level_apps == 2 && application_is_muted == TRUE)))
                      {
                          gchar *ignore_id = xfce_notify_log_insert(xndaemon->log,
                                                                    xndaemon->log_max_size_enabled ? xndaemon->log_max_size : -1,
                                                                    timestamp,
                                                                    new_app_name,
                                                                    summary,
                                                                    body,
                                                                    image_data,
                                                                    image_path,
                                                                    app_icon,
                                                                    desktop_id,
                                                                    expire_timeout,
                                                                    actions);
                          g_free(ignore_id);
                      }
            }

            xfce_notify_fdo_gbus_complete_notify (skeleton, invocation, OUT_id);
            if (image_data)
                g_variant_unref (image_data);
            if (desktop_id)
                g_free (desktop_id);
            g_date_time_unref(timestamp);
#ifdef ENABLE_SOUND
            if (sound_props != NULL) {
                ca_proplist_destroy(sound_props);
            }
#endif
            return TRUE;
        }
    }

    if(replaces_id
       && (window = g_tree_lookup(xndaemon->active_notifications,
                                  GUINT_TO_POINTER(replaces_id))))
    {
        xfce_notify_window_set_summary(window, summary);
        xfce_notify_window_set_body(window, body);
        xfce_notify_window_set_actions(window, actions, actions_are_icon_names, xndaemon->css_provider);
        xfce_notify_window_set_expire_timeout(window, expire_timeout);
        xfce_notify_window_set_opacity(window, xndaemon->initial_opacity);
#ifdef ENABLE_SOUND
        xfce_notify_window_set_sound_props(window, sound_props);
#endif
    } else {
        window = XFCE_NOTIFY_WINDOW(xfce_notify_window_new_with_actions(summary, body,
                                                                        app_icon,
                                                                        expire_timeout,
                                                                        actions,
                                                                        actions_are_icon_names,
                                                                        xndaemon->css_provider));
        xfce_notify_window_set_id(window, OUT_id);
        xfce_notify_window_set_override_redirect(window, xndaemon->windows_use_override_redirect);
        xfce_notify_window_set_opacity(window, xndaemon->initial_opacity);
#ifdef ENABLE_SOUND
        xfce_notify_window_set_sound_props(window, sound_props);
#endif

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

    if (image_data) {
        pix = notify_pixbuf_from_image_data(image_data);
        if (pix) {
            xfce_notify_window_set_icon_pixbuf(window, pix);
            g_object_unref(G_OBJECT(pix));
        }
    }
    else if (image_path) {
        xfce_notify_window_set_icon_name (window, image_path);
    }
    else if (app_icon && (g_strcmp0 (app_icon, "") != 0)) {
        xfce_notify_window_set_icon_name (window, app_icon);
    }
    else if (icon_data) {
        pix = notify_pixbuf_from_image_data(icon_data);
        if (pix) {
            xfce_notify_window_set_icon_pixbuf(window, pix);
            g_object_unref(G_OBJECT(pix));
        }
    }
    else if (desktop_id) {
        app_icon = notify_get_from_desktop_file (desktop_id, G_KEY_FILE_DESKTOP_KEY_ICON);

        xfce_notify_window_set_icon_name (window, app_icon);
    }

    if (xndaemon->notification_log == TRUE &&
        xndaemon->log != NULL &&
        xndaemon->log_level == 1 &&
        xndaemon->log_level_apps <= 1 &&
        transient == FALSE &&
        !g_hash_table_contains(xndaemon->excluded_from_log, new_app_name))
    {
        gchar *log_id = xfce_notify_log_insert(xndaemon->log,
                                               xndaemon->log_max_size_enabled ? xndaemon->log_max_size : -1,
                                               timestamp,
                                               new_app_name,
                                               summary,
                                               body,
                                               image_data,
                                               image_path,
                                               app_icon,
                                               desktop_id,
                                               expire_timeout,
                                               actions);
        xfce_notify_window_set_log_id(window, log_id);
        g_free(log_id);
    }

    xfce_notify_window_set_icon_only(window, x_canonical);

    xfce_notify_window_set_do_fadeout(window, xndaemon->do_fadeout, xndaemon->do_slideout);
    xfce_notify_window_set_notify_location(window, xndaemon->notify_location);

    if (value_hint_set) {
        xfce_notify_window_set_gauge_value(window, value_hint, xndaemon->css_provider);
        if (!xndaemon->show_text_with_gauge) {
            xfce_notify_window_set_summary(window, NULL);
            xfce_notify_window_set_body(window, NULL);
        }
    } else {
        xfce_notify_window_unset_gauge_value(window);
    }

    gtk_widget_realize(GTK_WIDGET(window));

    xfce_notify_fdo_gbus_complete_notify(skeleton, invocation, OUT_id);

    if (image_data)
      g_variant_unref (image_data);
    if (icon_data)
      g_variant_unref (icon_data);
    if (desktop_id)
        g_free (desktop_id);
    g_date_time_unref(timestamp);

    return TRUE;
}


static gboolean
notify_close_notification(XfceNotifyFdoGBus *skeleton,
                          GDBusMethodInvocation *invocation,
                          guint id,
                          XfceNotifyDaemon *xndaemon)
{
    XfceNotifyWindow *window = g_tree_lookup(xndaemon->active_notifications,
                                             GUINT_TO_POINTER(id));

    if(window)
        xfce_notify_window_closed(window, XFCE_NOTIFY_CLOSE_REASON_CLIENT);

    xfce_notify_fdo_gbus_complete_close_notification(skeleton, invocation);

    return TRUE;
}

static gboolean
notify_get_server_information (XfceNotifyFdoGBus *skeleton,
                               GDBusMethodInvocation *invocation,
                               XfceNotifyDaemon *xndaemon)
{
    xfce_notify_fdo_gbus_complete_get_server_information(skeleton,
                                                         invocation,
                                                         "Xfce Notify Daemon",
                                                         "Xfce",
                                                         VERSION,
                                                         NOTIFICATIONS_SPEC_VERSION);

    return TRUE;
}


static void
daemon_quit(XfceNotifyDaemon *xndaemon) {
    gint i, main_level = gtk_main_level();
    for(i = 0; i < main_level; ++i)
        gtk_main_quit();
}


static gboolean
xfce_notify_old_quit(XfceNotifyOldGBus *skeleton, GDBusMethodInvocation *invocation, XfceNotifyDaemon *xndaemon) {
    g_message("Method org.freedesktop.Notifications.Quit is deprecated and will be removed in a future version; please use org.xfce.Notifyd.Quit instead");
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    xfce_notify_old_gbus_complete_quit(skeleton, invocation);
G_GNUC_END_IGNORE_DEPRECATIONS
    daemon_quit(xndaemon);
    return TRUE;
}


static gboolean
xfce_notify_quit(XfceNotifyGBus *skeleton, GDBusMethodInvocation *invocation, XfceNotifyDaemon *xndaemon) {
    xfce_notify_gbus_complete_quit(skeleton, invocation);
    daemon_quit(xndaemon);
    return TRUE;
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
        g_free(file);
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

    if (g_strcmp0(property, "/theme") == 0) {
        xfce_notify_daemon_set_theme(xndaemon,
                                     G_VALUE_TYPE(value) == G_TYPE_STRING
                                     ? g_value_get_string(value)
                                     : "Default");
    } else if (g_strcmp0(property, DENIED_CRITICAL_NOTIFICATIONS_PROP) == 0
               || g_strcmp0(property, EXCLUDED_FROM_LOG_APPLICATIONS_PROP) == 0)
    {
        GHashTable *hashtable;

        if (g_strcmp0(property, DENIED_CRITICAL_NOTIFICATIONS_PROP) == 0) {
            hashtable = xndaemon->denied_critical_notifications;
        } else if (g_strcmp0(property, EXCLUDED_FROM_LOG_APPLICATIONS_PROP) == 0) {
            hashtable = xndaemon->excluded_from_log;
        } else {
            return;
        }

        g_hash_table_remove_all(hashtable);

        if (G_VALUE_TYPE(value) == G_TYPE_PTR_ARRAY) {
            GPtrArray *strs = g_value_get_boxed(value);

            for (guint i = 0; i < strs->len; ++i) {
                GValue *str_value = g_ptr_array_index(strs, i);
                if (G_VALUE_HOLDS_STRING(str_value)) {
                    g_hash_table_insert(hashtable,
                                        g_value_dup_string(str_value),
                                        GUINT_TO_POINTER(TRUE));
                }
            }
        }
    } else {
        for (gsize i = 0; i < G_N_ELEMENTS(settings); ++i) {
            if (g_strcmp0(property, settings[i].name) == 0) {
                guint8 *loc = ((guint8 *)xndaemon) + settings[i].offset;
                switch (settings[i].type) {
                    case G_TYPE_INT:
                        *(gint *)loc = G_VALUE_TYPE(value) == settings[i].type
                            ? g_value_get_int(value)
                            : settings[i].default_value.i;
                        break;

                    case G_TYPE_UINT:
                        *(guint *)loc = G_VALUE_TYPE(value) == settings[i].type
                            ? g_value_get_uint(value)
                            : settings[i].default_value.u;
                        break;

                    case G_TYPE_DOUBLE:
                        *(gdouble *)loc = G_VALUE_TYPE(value) == settings[i].type
                            ? g_value_get_double(value)
                            : settings[i].default_value.d;
                        break;

                    case G_TYPE_BOOLEAN:
                        *(gboolean *)loc = G_VALUE_TYPE(value) == settings[i].type
                            ? g_value_get_boolean(value)
                            : settings[i].default_value.b;
                        break;

                    default:
                        g_critical("Unhandled property type %s", g_type_name(settings[i].type));
                        break;
                }

                if (settings[i].offset == G_STRUCT_OFFSET(XfceNotifyDaemon, expire_timeout)) {
                    if (xndaemon->expire_timeout != -1) {
                        xndaemon->expire_timeout *= 1000;
                    }
                }
                break;
            }
        }
    }
}


static gboolean
xfce_notify_daemon_load_config (XfceNotifyDaemon *xndaemon,
                               	GError **error)
{
    gchar **strs;
    gchar *theme;

    xndaemon->settings = xfconf_channel_new("xfce4-notifyd");
    xfce_notify_migrate_log_max_size_setting(xndaemon->settings);

    theme = xfconf_channel_get_string(xndaemon->settings,
                                      "/theme", "Default");
    xfce_notify_daemon_set_theme(xndaemon, theme);
    g_free(theme);

    strs = xfconf_channel_get_string_list(xndaemon->settings, DENIED_CRITICAL_NOTIFICATIONS_PROP);
    if (strs != NULL) {
        for (gint i = 0; strs[i] != NULL; ++i) {
            g_hash_table_insert(xndaemon->denied_critical_notifications, strs[i], GUINT_TO_POINTER(TRUE));
        }
        g_free(strs);  // hash table owns the elements now
        strs = NULL;
    }

    strs = xfconf_channel_get_string_list(xndaemon->settings, EXCLUDED_FROM_LOG_APPLICATIONS_PROP);
    if (strs != NULL) {
        for (gint i = 0; strs[i] != NULL; ++i) {
            g_hash_table_insert(xndaemon->excluded_from_log, strs[i], GUINT_TO_POINTER(TRUE));
        }
        g_free(strs);  // hash table owns the elements now
        strs = NULL;
    }


    for (gsize i = 0; i < G_N_ELEMENTS(settings); ++i) {
        guint8 *loc = ((guint8 *)xndaemon) + settings[i].offset;
        switch (settings[i].type) {
            case G_TYPE_INT:
                *(gint *)loc = xfconf_channel_get_int(xndaemon->settings,
                                                      settings[i].name,
                                                      settings[i].default_value.i);
                break;

            case G_TYPE_UINT:
                *(guint *)loc = xfconf_channel_get_uint(xndaemon->settings,
                                                        settings[i].name,
                                                        settings[i].default_value.u);
                break;

            case G_TYPE_DOUBLE:
                *(gdouble *)loc = xfconf_channel_get_double(xndaemon->settings,
                                                            settings[i].name,
                                                            settings[i].default_value.d);
                break;

            case G_TYPE_BOOLEAN:
                *(gboolean *)loc = xfconf_channel_get_bool(xndaemon->settings,
                                                           settings[i].name,
                                                           settings[i].default_value.b);
                break;

            default:
                g_critical("Unhandled property type %s", g_type_name(settings[i].type));
                break;
        }
    }

    if (xndaemon->expire_timeout != -1) {
        xndaemon->expire_timeout *= 1000;
    }

    /* Clean up old notifications from the backlog */
    xfconf_channel_reset_property (xndaemon->settings, "/backlog", TRUE);

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
