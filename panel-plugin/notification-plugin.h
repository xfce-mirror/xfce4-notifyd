/*  xfce4-notification-plugin
 *
 *  Copyright (C) 2017 Simon Steinbeiß <simon@xfce.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __NOTIFICATION_PLUGIN_H__
#define __NOTIFICATION_PLUGIN_H__

G_BEGIN_DECLS

#include <xfconf/xfconf.h>

#define ICON_NAME                 "xfce4-notifyd"
#define XFCE_NOTIFY_LOG_FILE      "xfce4/notifyd/log"
#define SETTING_LOG_DISPLAY_LIMIT "/plugin/log-display-limit"
#define DEFAULT_LOG_DISPLAY_LIMIT 10
#define SETTING_LOG_ONLY_TODAY    "/plugin/log-only-today"

/* plugin structure */
typedef struct
{
    XfcePanelPlugin *plugin;
    XfconfChannel   *channel;

    /* panel widgets */
    GtkWidget       *button;
    GtkWidget       *image;
    GtkWidget       *menu;
}
NotificationPlugin;

void dnd_toggled_cb           (GtkCheckMenuItem   *checkmenuitem,
                               gpointer            user_data);

G_END_DECLS

#endif /* !__NOTIFICATION_PLUGIN_H__ */
