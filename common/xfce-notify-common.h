/*
 *  xfce4-notifyd
 *
 *  Copyright (c) 2023 Brian Tarricone <brian@tarricone.org>
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

#ifndef __XFCE_NOTIFY_COMMON_H__
#define __XFCE_NOTIFY_COMMON_H__

#include <gtk/gtk.h>
#include <xfconf/xfconf.h>

#define KNOWN_APPLICATIONS_PROP             "/applications/known_applications"
#define MUTED_APPLICATIONS_PROP             "/applications/muted_applications"
#define DENIED_CRITICAL_NOTIFICATIONS_PROP  "/applications/denied-critical-notifications"
#define EXCLUDED_FROM_LOG_APPLICATIONS_PROP "/applications/excluded-from-log"
#define MUTE_SOUNDS_PROP                    "/mute-sounds"
#define DATETIME_FORMAT_PROP                "/date-time-format"
#define DATETIME_CUSTOM_FORMAT_PROP         "/date-time-custom-format"
#define LOG_MAX_SIZE_ENABLED_PROP           "/log-max-size-enabled"
#define LOG_MAX_SIZE_PROP                   "/log-max-size"
#define DND_ENABLED_PROP                    "/do-not-disturb"
#define GAUGE_IGNORES_DND_PROP              "/gauge-ignores-dnd"
#define EXPIRE_TIMEOUT_ENABLED_PROP         "/expire-timeout-enabled"
#define EXPIRE_TIMEOUT_PROP                 "/expire-timeout"
#define EXPIRE_TIMEOUT_ALLOW_OVERRIDE_PROP  "/expire-timeout-allow-override"
#define NOTIFICATION_DISPLAY_FIELDS_PROP    "/notification-display-fields"
#define SHOW_NOTIFICATIONS_ON_PROP          "/show-notifications-on"
#define DO_FADEOUT_PROP                     "/do-fadeout"
#define DO_SLIDEOUT_PROP                    "/do-slideout"

#define DATETIME_CUSTOM_FORMAT_DEFAULT      "%a %H:%M:%S"
#define LOG_MAX_SIZE_DEFAULT                1000
#define EXPIRE_TIMEOUT_DEFAULT              10
#define DISPLAY_FIELDS_DEFAULT              XFCE_NOTIFY_DISPLAY_FULL
#define SHOW_NOTIFICATIONS_ON_DEFAULT       XFCE_NOTIFY_SHOW_ON_ACTIVE_MONITOR

// This is a hidden setting that restores the old behabior of using an
// override-redirect window for the notification windows.  This should be
// unnecessary for many/most WMs, but some (like openbox) won't ever place
// notifications above fullscreen windows (for example) without it.  The
// downside here is that notifications can pop up over your screen saver
// or screen locker, which many people might consider a security issue.  For
// that reason, this option will remain hidden (not presented in the GUI),
// and will be disabled by default.
#define COMPAT_OVERRIDE_REDIRECT_PROP       "/compat/use-override-redirect-windows"

G_BEGIN_DECLS

// NB: do not change the suffixes on these enums, as the string ("nick")
// versions of them are used as xfconf setting values.
typedef enum {
    XFCE_NOTIFY_DISPLAY_FULL,
    XFCE_NOTIFY_DISPLAY_ICON_SUMMARY,
    XFCE_NOTIFY_DISPLAY_ICON_APPNAME,
} XfceNotifyDisplayFields;

// NB: do not change the suffixes on these enums, as the string ("nick")
// versions of them are used as xfconf setting values.
typedef enum {
    XFCE_NOTIFY_SHOW_ON_ACTIVE_MONITOR,
    XFCE_NOTIFY_SHOW_ON_PRIMARY_MONITOR,
    XFCE_NOTIFY_SHOW_ON_ALL_MONITORS,
} XfceNotifyShowOn;

typedef enum {
    XFCE_LOG_LEVEL_ONLY_DND_OR_FIELDS_HIDDEN = 0,
    XFCE_LOG_LEVEL_ALWAYS = 1,
} XfceLogLevel;

typedef enum {
    XFCE_LOG_LEVEL_APPS_ALL = 0,
    XFCE_LOG_LEVEL_APPS_EXCEPT_BLOCKED = 1,
    XFCE_LOG_LEVEL_APPS_ONLY_BLOCKED = 2,
} XfceLogLevelApps;

typedef enum {
    XFCE_NOTIFY_URGENCY_LOW = 0,
    XFCE_NOTIFY_URGENCY_NORMAL,
    XFCE_NOTIFY_URGENCY_CRITICAL,
} XfceNotifyUrgency;

typedef enum {
    XFCE_NOTIFY_CLOSE_REASON_EXPIRED = 1,
    XFCE_NOTIFY_CLOSE_REASON_DISMISSED,
    XFCE_NOTIFY_CLOSE_REASON_CLIENT,
    XFCE_NOTIFY_CLOSE_REASON_UNKNOWN,
} XfceNotifyCloseReason;


gboolean xfce_notify_is_markup_valid(const gchar *markup);

GtkWidget *xfce_notify_create_placeholder_label(const gchar *markup);

gint xfce_notify_enum_value_from_nick(GType enum_type,
                                      const gchar *nick,
                                      gint default_value);
gchar *xfce_notify_enum_nick_from_value(GType enum_type,
                                        gint value);

void xfce_notify_migrate_log_max_size_setting(XfconfChannel *channel);
void xfce_notify_migrate_show_notifications_on_setting(XfconfChannel *channel);

G_END_DECLS

#endif  /* __XFCE_NOTIFY_COMMON_H__ */
