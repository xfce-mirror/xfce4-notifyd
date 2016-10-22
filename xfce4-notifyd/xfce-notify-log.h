/*
 *  xfce4-notifyd
 *
 *  Copyright (c) 2016 Simon Steinbeiß <ochosi@xfce.org>
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

#ifndef __XFCE_NOTIFY_LOG_H_
#define __XFCE_NOTIFY_LOG_H_


#define XFCE_NOTIFY_LOG_FILE "xfce4/notifyd/log"

GKeyFile   *xfce_notify_log_get (void);
void    xfce_notify_log_insert (const gchar *app_name,
                                const gchar *summary,
                                const gchar *body,
                                const gchar *app_icon,
                                gint expire_timeout,
                                const gchar **actions);
void    xfce_notify_log_clear (void);

#endif /* __XFCE_NOTIFY_LOG_H_ */
