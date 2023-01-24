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

#include <gtk/gtk.h>

#define XFCE_NOTIFY_LOG_FILE  "xfce4/notifyd/log"
#define XFCE_NOTIFY_ICON_PATH "xfce4/notifyd/icons/"

G_BEGIN_DECLS

GdkPixbuf *notify_pixbuf_from_image_data (GVariant *image_data);

gchar     *notify_get_from_desktop_file (const gchar *desktop_file,
                                         const gchar *key);

GKeyFile  *xfce_notify_log_get (void);

void       xfce_notify_log_insert (const gchar *app_name,
                                   const gchar *summary,
                                   const gchar *body,
                                   GVariant *image_data,
                                   const gchar *image_path,
                                   const gchar *app_icon,
                                   const gchar *desktop_id,
                                   gint expire_timeout,
                                   const gchar **actions,
                                   gint log_max_size);

GtkWidget *xfce_notify_clear_log_dialog (void);

void xfce_notify_log_clear (void);

gchar *notify_log_format_timestamp(const gchar *timestamp);
gchar *notify_log_format_summary(GKeyFile *notify_log, const gchar *group);
gchar *notify_log_format_body(GKeyFile *notify_log, const gchar *group);
cairo_surface_t *notify_log_load_icon(GKeyFile *notify_log,
                                      const gchar *group,
                                      const gchar *notify_log_icon_folder,
                                      gint size,
                                      gint scale);
gchar *notify_log_format_tooltip(const gchar *app_name, const gchar *timestamp, const gchar *body_text);

G_END_DECLS

#endif /* __XFCE_NOTIFY_LOG_H_ */
