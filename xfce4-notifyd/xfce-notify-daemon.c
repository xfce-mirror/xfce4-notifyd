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

#include "xfce-notification.h"
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
    XfceNotifyDisplayFields display_fields;
    gboolean do_not_disturb;
    gboolean gauge_ignores_dnd;
    gboolean notification_log;
    gboolean show_text_with_gauge;
    XfceNotifyShowOn show_notifications_on;
    gboolean windows_use_override_redirect;

    XfceNotifyDaemonLog *xndlog;
    XfceNotifyLog *log;
    XfceLogLevel log_level;
    XfceLogLevelApps log_level_apps;
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

typedef struct {
    const gchar *nick;
    gint value;
} EnumMapping;

static const EnumMapping notification_display_fields_mapping[] = {
    { "icon-summary-body", XFCE_NOTIFY_DISPLAY_FULL },
    { "icon-summary", XFCE_NOTIFY_DISPLAY_SUMMARY },
    { "icon-appname", XFCE_NOTIFY_DISPLAY_APP_NAME },
    { NULL, -1 },
};

static const EnumMapping show_notifications_on_mapping[] = {
    { "active-monitor", XFCE_NOTIFY_SHOW_ON_ACTIVE_MONITOR },
    { "primary-monitor", XFCE_NOTIFY_SHOW_ON_PRIMARY_MONITOR },
    { "all-monitors", XFCE_NOTIFY_SHOW_ON_ALL_MONITORS },
    { NULL, -1 },
};

// This deliberately leaves out '/theme', as that one is handled in
// a special way.
static const struct {
    const gchar *name;
    GType type;
    gsize offset;
    union {
        gint i;
        guint u;
        gdouble d;
        gboolean b;
        const gchar *s;
    } default_value;
    const EnumMapping *enum_mappings;
    gint enum_default_value;
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
        .name = DO_FADEOUT_PROP,
        .type = G_TYPE_BOOLEAN,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, do_fadeout),
        .default_value.b = TRUE,
    },
    {
        .name = DO_SLIDEOUT_PROP,
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
        .name = SHOW_NOTIFICATIONS_ON_PROP,
        .type = G_TYPE_STRING,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, show_notifications_on),
        .default_value.s = SHOW_NOTIFICATIONS_ON_DEFAULT,
        .enum_mappings = show_notifications_on_mapping,
        .enum_default_value = XFCE_NOTIFY_SHOW_ON_ACTIVE_MONITOR,
    },
    {
        .name = NOTIFICATION_DISPLAY_FIELDS_PROP,
        .type = G_TYPE_STRING,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, display_fields),
        .default_value.s = DISPLAY_FIELDS_DEFAULT,
        .enum_mappings = notification_display_fields_mapping,
        .enum_default_value = XFCE_NOTIFY_DISPLAY_FULL,
    },
    {
        .name = "/do-not-disturb",
        .type = G_TYPE_BOOLEAN,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, do_not_disturb),
        .default_value.b = FALSE,
    },
    {
        .name = GAUGE_IGNORES_DND_PROP,
        .type = G_TYPE_BOOLEAN,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, gauge_ignores_dnd),
        .default_value.b = TRUE,
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
        .default_value.u = XFCE_LOG_LEVEL_ONLY_DND_OR_FIELDS_HIDDEN,
    },
    {
        .name = "/log-level-apps",
        .type = G_TYPE_UINT,
        .offset = G_STRUCT_OFFSET(XfceNotifyDaemon, log_level_apps),
        .default_value.u = XFCE_LOG_LEVEL_APPS_ALL,
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
xfce_uint_compare(gconstpointer a,
                  gconstpointer b,
                  gpointer user_data)
{
    guint ia = GPOINTER_TO_UINT(a);
    guint ib = GPOINTER_TO_UINT(b);

    if (ib >= ia) {
        return ib - ia;
    } else {
        return -(gint)(ia - ib);
    }
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

    xfce_notify_daemon_init_placement_data(self);

#ifdef ENABLE_X11
    if (GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
        /* Monitor root window changes */
        GdkScreen *screen = gdk_screen_get_default();
        GdkWindow *groot = gdk_screen_get_root_window(screen);
        gdk_window_set_events(groot, gdk_window_get_events(groot) | GDK_PROPERTY_CHANGE_MASK);
        gdk_window_add_filter(groot, xfce_notify_rootwin_watch_workarea, self);
    }
#endif
}

static void
xfce_notify_daemon_init(XfceNotifyDaemon *xndaemon)
{
    GError *error = NULL;

    xndaemon->active_notifications = g_tree_new_full(xfce_uint_compare,
                                                     NULL, NULL,
                                                     (GDestroyNotify)g_object_unref);

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
xfce_notify_daemon_action_invoked(XfceNotification *notification,
                                  const gchar *action,
                                  XfceNotifyDaemon *xndaemon)
{
    guint id = xfce_notification_get_id(notification);

    g_return_if_fail(id > 0);

    xfce_notify_fdo_gbus_emit_action_invoked(XFCE_NOTIFY_FDO_GBUS(xndaemon),
                                             id,
                                             action);
}

static void
xfce_notify_daemon_closed(XfceNotification *notification,
                          XfceNotifyCloseReason reason,
                          XfceNotifyDaemon *xndaemon)
{
    guint id = xfce_notification_get_id(notification);

    for (GList *l = xfce_notification_get_windows(notification); l != NULL; l = l->next) {
        XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(l->data);
        GdkMonitor *monitor = xfce_notify_window_get_monitor(window);
        gint monitor_num = xfce_notify_daemon_get_monitor_index(gdk_display_get_default(), monitor);

        if (G_LIKELY(monitor_num >= 0)) {
            /* Remove the reserved rectangle from the list */
            GList *list = xndaemon->reserved_rectangles[monitor_num];
            list = g_list_remove(list, xfce_notify_window_get_geometry(window));
            xndaemon->reserved_rectangles[monitor_num] = list;
        }
    }

    if (reason == XFCE_NOTIFY_CLOSE_REASON_DISMISSED && xndaemon->log != NULL) {
        const gchar *log_id = xfce_notification_get_log_id(notification);

        if (log_id != NULL) {
            xfce_notify_log_mark_read(xndaemon->log, log_id);
        }
    }

    g_return_if_fail(id > 0);
    g_tree_remove(xndaemon->active_notifications, GUINT_TO_POINTER(id));
    xfce_notify_fdo_gbus_emit_notification_closed(XFCE_NOTIFY_FDO_GBUS(xndaemon),
                                                  id,
                                                  (guint)reason);
}

static void
xfce_notify_daemon_place_notification_window(XfceNotifyDaemon *xndaemon,
                                             XfceNotifyWindow *window,
                                             GdkMonitor *monitor)
{
    GtkWidget *widget = GTK_WIDGET(window);
    GdkScreen *screen;
    GtkAllocation allocation;
    GdkRectangle geom, initial, widget_geom;
    gint monitor_num;
    gint max_width;

    if (gtk_widget_has_screen(widget)) {
        screen = gtk_widget_get_screen(widget);
    } else {
        screen = gdk_screen_get_default();
        gtk_window_set_screen(GTK_WINDOW(widget), screen);
    }

    gtk_widget_get_allocation(widget, &allocation);
    monitor_num = xfce_notify_daemon_get_monitor_index(gdk_screen_get_display(screen), monitor);
    g_return_if_fail(monitor_num >= 0);
    geom = xndaemon->monitors_workarea[monitor_num];

    DBG("placing window, allocation=%dx%d+%d+%d", allocation.width, allocation.height, allocation.x, allocation.y);

    /* Set initial geometry */
    initial.width = allocation.width;
    initial.height = allocation.height;

    switch(xndaemon->notify_location) {
        case GTK_CORNER_TOP_LEFT:
            initial.x = geom.x + SPACE;
            initial.y = geom.y + SPACE;
            break;
        case GTK_CORNER_BOTTOM_LEFT:
            initial.x = geom.x + SPACE;
            initial.y = geom.y + geom.height - allocation.height - SPACE;
            break;
        case GTK_CORNER_TOP_RIGHT:
            initial.x = geom.x + geom.width - allocation.width - SPACE;
            initial.y = geom.y + SPACE;
            break;
        case GTK_CORNER_BOTTOM_RIGHT:
            initial.x = geom.x + geom.width - allocation.width - SPACE;
            initial.y = geom.y + geom.height - allocation.height - SPACE;
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

    if (xndaemon->reserved_rectangles[monitor_num] != NULL) {
        gboolean found = FALSE;

        /* Else, we try to find the appropriate position on the monitor */
        while(!found) {
            gboolean overlaps = FALSE;
            GList *l = NULL;
            gint notification_y, notification_height;

            DBG("Test if the candidate overlaps one of the existing notifications.");

            for(l = xndaemon->reserved_rectangles[monitor_num]; l; l = l->next) {
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

    DBG("Move the notification to: x=%i, y=%i", widget_geom.x, widget_geom.y);
    xfce_notify_window_set_geometry(XFCE_NOTIFY_WINDOW(widget), widget_geom);
    xndaemon->reserved_rectangles[monitor_num] = g_list_prepend(xndaemon->reserved_rectangles[monitor_num],
                                                                xfce_notify_window_get_geometry(XFCE_NOTIFY_WINDOW(widget)));
}

static void
xfce_notify_daemon_window_size_allocate(GtkWidget *widget,
                                        GtkAllocation *allocation,
                                        gpointer user_data)
{
    XfceNotifyDaemon *xndaemon = user_data;
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(widget);
    GdkScreen *widget_screen;
    GdkDisplay *display;
    GdkMonitor *monitor;
    gint monitor_num;
    GdkRectangle old_geom, geom;
    gint cur_x, cur_y;

    DBG("Size allocate called for %d", xfce_notify_window_get_id(window));

    gtk_widget_set_allocation(widget, allocation);

    old_geom = *xfce_notify_window_get_geometry(window);

#ifdef ENABLE_X11
    if (GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
        gtk_window_get_position(GTK_WINDOW(widget), &cur_x, &cur_y);
    } else
#endif
    {
        cur_x = old_geom.x;
        cur_y = old_geom.y;
    }

    widget_screen = gtk_widget_get_screen (widget);
    display = gdk_screen_get_display(widget_screen);

    monitor = xfce_notify_window_get_monitor(window);
    monitor_num = xfce_notify_daemon_get_monitor_index(display, monitor);

    DBG("We are on the monitor %i", monitor_num);

    geom = xndaemon->monitors_workarea[monitor_num];

    DBG("Workarea: (%i,%i), width: %i, height:%i",
        geom.x, geom.y, geom.width, geom.height);

    if(old_geom.width != 0 && old_geom.height != 0) {
        /* Notification has already been placed previously. */
        GdkRectangle geom_union;
        gdk_rectangle_union(&old_geom, &geom, &geom_union);
        if (allocation->width == old_geom.width
            && allocation->height == old_geom.height
            && gdk_rectangle_equal(&geom, &geom_union)
            && old_geom.x == cur_x
            && old_geom.y == cur_y)
        {
            /* No updates are necessary */
            return;
        } else {
            xndaemon->reserved_rectangles[monitor_num] = g_list_remove(xndaemon->reserved_rectangles[monitor_num],
                                                                       xfce_notify_window_get_geometry(window));
        }
    }

    xfce_notify_daemon_place_notification_window(xndaemon, window, monitor);
}

static gboolean
xfce_notify_daemon_update_reserved_rectangles(gpointer key,
                                              gpointer value,
                                              gpointer data)
{
    XfceNotifyDaemon *xndaemon = XFCE_NOTIFY_DAEMON(data);
    XfceNotification *notification = XFCE_NOTIFICATION(value);
    GList *windows = xfce_notification_get_windows(notification);

    for (GList *l = windows; l != NULL; l = l->next) {
        XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(l->data);
        GdkMonitor *monitor = xfce_notify_window_get_monitor(window);
        xfce_notify_daemon_place_notification_window(xndaemon, window, monitor);
    }

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
notify_show_windows(gpointer data) {
    XfceNotification *notification = data;

    for (GList *l = xfce_notification_get_windows(notification); l != NULL; l = l->next) {
        gtk_widget_show(GTK_WIDGET(l->data));
    }
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
notify_update_theme_foreach(gpointer key, gpointer value, gpointer data) {
    XfceNotifyDaemon *xndaemon = XFCE_NOTIFY_DAEMON(data);
    XfceNotification *notification = XFCE_NOTIFICATION(value);
    GList *windows = xfce_notification_get_windows(notification);

    for (GList *l = windows; l != NULL; l = l->next) {
        GtkWidget *window = GTK_WIDGET(l->data);
        notify_update_theme_for_window(xndaemon, window, TRUE);
    }

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
    g_value_set_string (val, new_app_name);

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
                       const gchar *icon_id,
                       const gchar *app_icon,
                       const gchar *desktop_id,
                       gint expire_timeout,
                       XfceNotificationActions *actions)
{
    gchar *id = NULL;
    XfceNotifyLogEntry *entry = xfce_notify_log_entry_new_empty();
    entry->timestamp = g_date_time_ref(timestamp);
    entry->app_id = g_strdup(app_id);
    entry->icon_id = g_strdup(icon_id);
    entry->summary = g_strdup(summary);
    entry->body = g_strdup(body);
    entry->expire_timeout = expire_timeout;
    entry->is_read = FALSE;

    for (guint i = 0; i < actions->n_actions; ++i) {
        XfceNotifyLogEntryAction *action = g_new0(XfceNotifyLogEntryAction, 1);
        action->id = g_strdup(actions->actions[i].id);
        action->label = g_strdup(actions->actions[i].label);
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
              const gchar **actions_data,
              GVariant *hints,
              gint expire_timeout,
              XfceNotifyDaemon *xndaemon)
{
    XfceNotification *notification = NULL;
    XfceNotificationActions *actions;
    const gchar **cur_action_id;
    guint n_actions = 0;
    gchar *log_id = NULL;
    GVariant *image_data = NULL;
    GVariant *icon_data = NULL;
    const gchar *image_path = NULL;
    gchar *desktop_id = NULL;
    gchar *icon_name = NULL;
    GdkPixbuf *icon_pixbuf = NULL;
    gchar *icon_id = NULL;
    gchar *new_app_name;
    gint value_hint = 0;
    gint urgency = XFCE_NOTIFY_URGENCY_NORMAL;
    gboolean value_hint_set = FALSE;
    gboolean icon_only = FALSE;
    gboolean transient = FALSE;
    gboolean actions_are_icon_names = FALSE;
    gboolean log_level_ok = FALSE;
    gboolean log_level_apps_ok = FALSE;
#ifdef ENABLE_SOUND
    gboolean has_sound = FALSE;
    ca_proplist *sound_props = NULL;
    int ca_err;
#endif
    GVariant *item;
    GVariantIter iter;
    guint OUT_id = replaces_id != 0 ? replaces_id : xfce_notify_daemon_generate_id(xndaemon);
    gboolean application_is_muted = FALSE;
    const gchar *summary_text;
    const gchar *body_text;

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
            icon_only = TRUE;
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

    if (urgency == XFCE_NOTIFY_URGENCY_CRITICAL && g_hash_table_contains(xndaemon->denied_critical_notifications, new_app_name)) {
        urgency = XFCE_NOTIFY_URGENCY_NORMAL;
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

    if (image_data != NULL) {
        icon_pixbuf = notify_pixbuf_from_image_data(image_data);
    } else if (image_path != NULL) {
        icon_name = g_strdup(image_path);
    } else if (app_icon != NULL && g_strcmp0(app_icon, "") != 0) {
        icon_name = g_strdup(app_icon);
    } else if (icon_data != NULL) {
        icon_pixbuf = notify_pixbuf_from_image_data(icon_data);
    } else if (desktop_id != NULL) {
        icon_name = notify_get_from_desktop_file(desktop_id, G_KEY_FILE_DESKTOP_KEY_ICON);
    }

    cur_action_id = actions_data;
    while (*cur_action_id != NULL) {
        if (cur_action_id[1] != NULL) {
            cur_action_id += 2;
            ++n_actions;
        }
    }
    actions = g_new0(XfceNotificationActions, 1);
    actions->ids_are_icon_names = actions_are_icon_names;
    actions->actions = g_new0(XfceNotificationAction, n_actions);
    actions->n_actions = n_actions;
    for (guint i = 0; i < actions->n_actions; ++i) {
        actions->actions[i].id = g_strdup(actions_data[i * 2]);
        actions->actions[i].label = g_strdup(actions_data[i * 2 + 1]);
    }

    switch (xndaemon->log_level) {
        case XFCE_LOG_LEVEL_ONLY_DND_OR_FIELDS_HIDDEN:
            log_level_ok = xndaemon->do_not_disturb || xndaemon->display_fields != XFCE_NOTIFY_DISPLAY_FULL;
            break;
        case XFCE_LOG_LEVEL_ALWAYS:
            log_level_ok = TRUE;
            break;
    }

    switch (xndaemon->log_level_apps) {
        case XFCE_LOG_LEVEL_APPS_ALL:
            log_level_apps_ok = TRUE;
            break;
        case XFCE_LOG_LEVEL_APPS_EXCEPT_BLOCKED:
            log_level_apps_ok = !application_is_muted;
            break;
        case XFCE_LOG_LEVEL_APPS_ONLY_BLOCKED:
            log_level_apps_ok = application_is_muted;
            break;
    }

    if (replaces_id > 0) {
        notification = g_tree_lookup(xndaemon->active_notifications,
                                     GUINT_TO_POINTER(replaces_id));
    }

    if (xndaemon->notification_log &&
        xndaemon->log != NULL &&
        log_level_ok &&
        log_level_apps_ok &&
        !transient &&
        !g_hash_table_contains(xndaemon->excluded_from_log, new_app_name))
    {
        icon_id = xfce_notify_log_cache_icon(image_data,
                                             image_path,
                                             app_icon,
                                             desktop_id);

        // Ensure we don't log a duplicate entry if nothing material about
        // an existing notification has changed.
        if (notification == NULL ||
            g_strcmp0(summary, xfce_notification_get_summary(notification)) != 0 ||
            g_strcmp0(body, xfce_notification_get_body(notification)) != 0 ||
            g_strcmp0(icon_id, xfce_notification_get_icon_id(notification)) != 0)
        {
            GDateTime *timestamp = g_date_time_new_now_local();
            log_id = xfce_notify_log_insert(xndaemon->log,
                                            xndaemon->log_max_size_enabled ? xndaemon->log_max_size : -1,
                                            timestamp,
                                            new_app_name,
                                            summary,
                                            body,
                                            icon_id,
                                            app_icon,
                                            desktop_id,
                                            expire_timeout,
                                            actions);
            g_date_time_unref(timestamp);
        }
    }

    switch (xndaemon->display_fields) {
        case XFCE_NOTIFY_DISPLAY_FULL:
            summary_text = summary;
            body_text = body;
            break;
        case XFCE_NOTIFY_DISPLAY_SUMMARY:
            summary_text = summary;
            body_text = NULL;
            break;
        case XFCE_NOTIFY_DISPLAY_APP_NAME:
            summary_text = new_app_name;
            body_text = NULL;
            break;
        default:
            g_assert_not_reached();
            break;
    }

    /* Don't show notification bubbles in the "Do not disturb" mode or if the
       application has been muted by the user. Exceptions are "urgent"
       notifications, or if it's a gauge notification and the user wants to let
       those through. */
    if (urgency != XFCE_NOTIFY_URGENCY_CRITICAL &&
        ((xndaemon->do_not_disturb && (!value_hint_set || !xndaemon->gauge_ignores_dnd)) || application_is_muted))
    {
        if (notification != NULL) {
            // We're in DnD and there's an existing notification.  The only way
            // that could happen is if the existing one is critical, but the
            // current call is to update it so it's not critical anymore.  In
            // this case, we'll do the update, but set a very short expiration
            // so it more or less immediately disappears.  We'll also drop
            // the sound props (if any), since in DnD mode we probably don't
            // want sounds.
            xfce_notification_update(notification,
                                     summary_text,
                                     body_text,
                                     icon_only,
                                     icon_name,
                                     icon_pixbuf,
                                     icon_id,
                                     value_hint,
                                     value_hint_set,
                                     actions,
                                     500,
                                     urgency
#ifdef ENABLE_SOUND
                                     , NULL
#endif
                                     );
        }

#ifdef ENABLE_SOUND
        if (sound_props != NULL) {
            ca_proplist_destroy(sound_props);
            sound_props = NULL;
        }
#endif
    } else if (notification != NULL) {
        xfce_notification_update(notification,
                                 summary_text,
                                 body_text,
                                 icon_only,
                                 icon_name,
                                 icon_pixbuf,
                                 icon_id,
                                 value_hint,
                                 value_hint_set,
                                 actions,
                                 expire_timeout,
                                 urgency
#ifdef ENABLE_SOUND
                                 , sound_props
#endif
                                 );
    } else {
        GdkDisplay *display = gdk_display_get_default();
        GList *monitors = NULL;

        notification = xfce_notification_new(OUT_id,
                                             log_id,
                                             summary_text,
                                             body_text,
                                             icon_only,
                                             icon_name,
                                             icon_pixbuf,
                                             icon_id,
                                             value_hint,
                                             value_hint_set,
                                             actions,
                                             expire_timeout,
                                             urgency,
                                             xndaemon->do_fadeout,
                                             xndaemon->do_slideout,
#ifdef ENABLE_SOUND
                                             sound_props,
#endif
                                             xndaemon->css_provider);

        g_assert(g_tree_lookup(xndaemon->active_notifications, GUINT_TO_POINTER(OUT_id)) == NULL);
        g_tree_insert(xndaemon->active_notifications,
                      GUINT_TO_POINTER(OUT_id), notification);

        g_signal_connect(notification, "action-invoked",
                         G_CALLBACK(xfce_notify_daemon_action_invoked), xndaemon);
        g_signal_connect(notification, "closed",
                         G_CALLBACK(xfce_notify_daemon_closed), xndaemon);

        switch (xndaemon->show_notifications_on) {
            case XFCE_NOTIFY_SHOW_ON_ACTIVE_MONITOR: {
                GdkSeat *seat = gdk_display_get_default_seat(display);
                GdkDevice *pointer = gdk_seat_get_pointer(seat);
                gint x, y;

                gdk_device_get_position(pointer, NULL, &x, &y);
                monitors = g_list_append(monitors, gdk_display_get_monitor_at_point(display, x, y));
                break;
            }

            case XFCE_NOTIFY_SHOW_ON_PRIMARY_MONITOR:
                monitors = g_list_append(monitors, gdk_display_get_primary_monitor(display));
                break;

            case XFCE_NOTIFY_SHOW_ON_ALL_MONITORS: {
                gint n_monitors = gdk_display_get_n_monitors(display);

                for (gint i = 0; i < n_monitors; ++i) {
                    monitors = g_list_prepend(monitors, gdk_display_get_monitor(display, i));
                }
                monitors = g_list_reverse(monitors);
                break;
            }
        }

        xfce_notification_realize(notification,
                                  monitors,
                                  xndaemon->windows_use_override_redirect,
                                  xndaemon->notify_location,
                                  xndaemon->initial_opacity,
                                  xndaemon->show_text_with_gauge);
        for (GList *l = xfce_notification_get_windows(notification); l != NULL; l = l->next) {
            GtkWidget *window = GTK_WIDGET(l->data);

            g_signal_connect(window, "size-allocate",
                             G_CALLBACK(xfce_notify_daemon_window_size_allocate), xndaemon);
            notify_update_theme_for_window(xndaemon, window, FALSE);
        }
        g_idle_add(notify_show_windows, notification);

        g_list_free(monitors);
    }

    xfce_notify_fdo_gbus_complete_notify(skeleton, invocation, OUT_id);
    if (notification == NULL) {
        // We never displayed a notification, so synthesize a NotificationClosed signal.
        xfce_notify_fdo_gbus_emit_notification_closed(skeleton, OUT_id, XFCE_NOTIFY_CLOSE_REASON_UNKNOWN);
   }

    if (image_data)
      g_variant_unref (image_data);
    if (icon_data)
      g_variant_unref (icon_data);
    g_free(icon_id);
    if (desktop_id)
        g_free (desktop_id);
    g_free(icon_name);
    if (icon_pixbuf != NULL) {
        g_object_unref(icon_pixbuf);
    }
    g_free(new_app_name);
    g_free(log_id);

    return TRUE;
}


static gboolean
notify_close_notification(XfceNotifyFdoGBus *skeleton,
                          GDBusMethodInvocation *invocation,
                          guint id,
                          XfceNotifyDaemon *xndaemon)
{
    XfceNotification *notification = g_tree_lookup(xndaemon->active_notifications,
                                                   GUINT_TO_POINTER(id));

    if (G_LIKELY(notification != NULL)) {
        xfce_notification_closed(notification, XFCE_NOTIFY_CLOSE_REASON_CLIENT);
    }

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



static gboolean
xfce_notify_daemon_collect_nonurgent_notifications(gpointer id_p,
                                                   gpointer notification_p,
                                                   gpointer user_data)
{
    XfceNotification *notification = XFCE_NOTIFICATION(notification_p);

    if (xfce_notification_get_urgency(notification) < XFCE_NOTIFY_URGENCY_CRITICAL) {
        GList **list = user_data;
        *list = g_list_prepend(*list, notification);
    }

    return FALSE;
}


static gboolean
xfce_notify_daemon_update_do_fadeout(gpointer id_p,
                                     gpointer notification_p,
                                     gpointer user_data)
{
    XfceNotification *notification = XFCE_NOTIFICATION(notification_p);
    XfceNotifyDaemon *xndaemon = XFCE_NOTIFY_DAEMON(user_data);

    xfce_notification_set_do_fadeout(notification, xndaemon->do_fadeout);

    return FALSE;
}


static gboolean
xfce_notify_daemon_update_do_slideout(gpointer id_p,
                                      gpointer notification_p,
                                      gpointer user_data)
{
    XfceNotification *notification = XFCE_NOTIFICATION(notification_p);
    XfceNotifyDaemon *xndaemon = XFCE_NOTIFY_DAEMON(user_data);

    xfce_notification_set_do_slideout(notification, xndaemon->do_slideout);

    return FALSE;
}


static gint
xfce_notify_convert_enum(const EnumMapping mappings[],
                         const gchar *property_name,
                         const gchar *str,
                         gint default_value)
{
    if (str == NULL) {
        return default_value;
    } else {
        for (gsize i = 0; mappings[i].nick != NULL; ++i) {
            if (strcmp(mappings[i].nick, str) == 0) {
                return mappings[i].value;
            }
        }

        g_warning("Invalid value '%s' for property '%s'; using default", str, property_name);
        return default_value;
    }
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

                    case G_TYPE_STRING: {
                        const gchar *str = G_VALUE_TYPE(value) == settings[i].type
                            ? g_value_get_string(value)
                            : settings[i].default_value.s;
                        if (settings[i].enum_mappings != NULL) {
                            *(gint *)loc = xfce_notify_convert_enum(settings[i].enum_mappings,
                                                                    settings[i].name,
                                                                    str,
                                                                    settings[i].enum_default_value);
                        } else {
                            *(gchar **)loc = g_strdup(str);
                        }
                        break;
                    }

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

        if (g_strcmp0(property, DND_ENABLED_PROP) == 0) {
            if (xndaemon->do_not_disturb) {
                GList *to_remove = NULL;
                g_tree_foreach(xndaemon->active_notifications, xfce_notify_daemon_collect_nonurgent_notifications, &to_remove);
                for (GList *l = to_remove; l != NULL; l = l->next) {
                    xfce_notification_closed(XFCE_NOTIFICATION(l->data), XFCE_NOTIFY_CLOSE_REASON_UNKNOWN);
                }
                g_list_free(to_remove);
            }
        } else if (g_strcmp0(property, DO_FADEOUT_PROP) == 0) {
            g_tree_foreach(xndaemon->active_notifications, xfce_notify_daemon_update_do_fadeout, xndaemon);
        } else if (g_strcmp0(property, DO_SLIDEOUT_PROP) == 0) {
            g_tree_foreach(xndaemon->active_notifications, xfce_notify_daemon_update_do_slideout, xndaemon);
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
    xfce_notify_migrate_show_notifications_on_setting(xndaemon->settings);

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

            case G_TYPE_STRING: {
                const gchar *value = xfconf_channel_get_string(xndaemon->settings,
                                                               settings[i].name,
                                                               settings[i].default_value.s);
                if (settings[i].enum_mappings != NULL) {
                    *(gint *)loc = xfce_notify_convert_enum(settings[i].enum_mappings,
                                                            settings[i].name,
                                                            value,
                                                            settings[i].enum_default_value);
                } else {
                    *(gchar **)loc = g_strdup(value);
                }
                break;
            }

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
