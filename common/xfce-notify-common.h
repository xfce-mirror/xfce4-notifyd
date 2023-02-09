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

#define DATETIME_CUSTOM_FORMAT_DEFAULT      "%a %H:%M:%S"

#define LOG_MAX_SIZE_DEFAULT                1000

G_BEGIN_DECLS

gboolean xfce_notify_is_markup_valid(const gchar *markup);

GtkWidget *xfce_notify_create_placeholder_label(const gchar *markup);

void xfce_notify_migrate_log_max_size_setting(XfconfChannel *channel);

G_END_DECLS

#endif  /* __XFCE_NOTIFY_COMMON_H__ */
