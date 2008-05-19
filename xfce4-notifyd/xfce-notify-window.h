/*
 *  xfce4-notifyd
 *
 *  Copyright (c) 2008 Brian Tarricone <bjt23@cornell.edu>
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

#ifndef __XFCE_NOTIFY_WINDOW_H__
#define __XFCE_NOTIFY_WINDOW_H__

#include <gtk/gtk.h>

#define XFCE_TYPE_NOTIFY_WINDOW     (xfce_notify_window_get_type())
#define XFCE_NOTIFY_WINDOW(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), XFCE_TYPE_NOTIFY_WINDOW, XfceNotifyWindow))
#define XFCE_IS_NOTIFY_WINDOW(obj)  (G_TYPE_CHECK_INSTANCE_TYPE((obj), XFCE_TYPE_NOTIFY_WINDOW))

G_BEGIN_DECLS

typedef enum
{
    XFCE_NOTIFY_CLOSE_REASON_EXPIRED = 1,
    XFCE_NOTIFY_CLOSE_REASON_DISMISSED,
    XFCE_NOTIFY_CLOSE_REASON_CLIENT,
    XFCE_NOTIFY_CLOSE_REASON_UNKNOWN,
} XfceNotifyCloseReason;

typedef struct _XfceNotifyWindow  XfceNotifyWindow;

GType xfce_notify_window_get_type() G_GNUC_CONST;

GtkWidget *xfce_notify_window_new();

GtkWidget *xfce_notify_window_new_full(const gchar *summary,
                                       const gchar *body,
                                       const gchar *icon_name,
                                       gint expire_timeout);

GtkWidget *xfce_notify_window_new_with_actions(const gchar *summary,
                                               const gchar *body,
                                               const gchar *icon_name,
                                               gint expire_timeout,
                                               const gchar **actions);

void xfce_notify_window_set_summary(XfceNotifyWindow *window,
                                    const gchar *summary);
void xfce_notify_window_set_body(XfceNotifyWindow *window,
                                 const gchar *body);

void xfce_notify_window_set_icon_name(XfceNotifyWindow *window,
                                      const gchar *icon_name);
void xfce_notify_window_set_icon_pixbuf(XfceNotifyWindow *window,
                                        GdkPixbuf *pixbuf);

void xfce_notify_window_set_expire_timeout(XfceNotifyWindow *window,
                                           gint expire_timeout);

void xfce_notify_window_set_actions(XfceNotifyWindow *window,
                                    const gchar **actions);

void xfce_notify_window_set_fade_transparent(XfceNotifyWindow *window,
                                             gboolean fade_transparent);
gboolean xfce_notify_window_get_fade_transparent(XfceNotifyWindow *window);

void xfce_notify_window_set_opacity(XfceNotifyWindow *window,
                                    gdouble opacity);
gdouble xfce_notify_window_get_opacity(XfceNotifyWindow *window);

/* signal trigger */
void xfce_notify_window_closed(XfceNotifyWindow *window,
                               XfceNotifyCloseReason reason);

G_END_DECLS

#endif  /* __XFCE_NOTIFY_WINDOW_H__ */
