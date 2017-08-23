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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <xfconf/xfconf.h>
#include <libxfce4util/libxfce4util.h>
#include <libxfce4panel/xfce-panel-plugin.h>

#include <glib.h>
#include "notification-plugin.h"
#include "notification-plugin-log.h"



GKeyFile *
xfce_notify_log_get (void)
{
    GKeyFile *notify_log;
    gchar *notify_log_path = NULL;

    notify_log_path = xfce_resource_lookup (XFCE_RESOURCE_CACHE, XFCE_NOTIFY_LOG_FILE);

    if (notify_log_path)
    {
        notify_log = g_key_file_new ();
        if (g_key_file_load_from_file (notify_log, notify_log_path, G_KEY_FILE_NONE, NULL) == FALSE)
            return NULL;
    }
    else
        return NULL;
    g_free (notify_log_path);

    return notify_log;
}



static void
notification_plugin_menu_clear (GtkWidget *widget, gpointer user_data)
{
    GtkWidget *container = user_data;
    gtk_container_remove (GTK_CONTAINER (container), widget);
}



void
notification_plugin_menu_populate (NotificationPlugin *notification_plugin)
{
  GtkMenu *menu = GTK_MENU (notification_plugin->menu);
  GtkWidget *mi;
  GKeyFile *notify_log;
  gint i;
  GDateTime *today;
  gchar *timestamp;
  gsize num_groups = 0;
  GtkCallback func = notification_plugin_menu_clear;

  today = g_date_time_new_now_local ();
  timestamp = g_date_time_format (today, "%F");

  /* Clean up the list and re-fill it */
  gtk_container_foreach (GTK_CONTAINER (menu), func, menu);

  notify_log = xfce_notify_log_get();

  if (notify_log) {
    gchar **groups;
    int log_length;

    groups = g_key_file_get_groups (notify_log, &num_groups);
    log_length = GPOINTER_TO_UINT(num_groups) - LOG_DISPLAY_LIMIT;
    if (log_length < 0)
      log_length = 0;

    /* Notifications are only shown until LOG_DISPLAY_LIMIT is hit */
    for (i = log_length; groups && groups[i]; i += 1) {
      GtkWidget *grid;
      GtkWidget *summary, *body, *app_icon, *expire_timeout;
      const gchar *group = groups[i];
      const char *format = "<b>\%s</b>";
      const char *tooltip_format = "<b>\%s</b> - \%s\n\%s";
      const char *tooltip_format_simple = "<b>\%s</b> - \%s";
      char *markup;
      gchar *app_name;
      gchar *tooltip_timestamp;
      gchar *tmp;
      GTimeVal tv;
      GDateTime *log_timestamp;

      /* only show notifications from today
      if (g_ascii_strncasecmp (timestamp, group, 10) == 0)
        break; */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      mi = gtk_image_menu_item_new ();
G_GNUC_END_IGNORE_DEPRECATIONS

      if (g_time_val_from_iso8601 (group, &tv) == TRUE) {
        if (log_timestamp = g_date_time_new_from_timeval_local (&tv)) {
          tooltip_timestamp = g_date_time_format (log_timestamp, "%c");
          g_date_time_unref(log_timestamp);
        }
      }

      app_name = g_key_file_get_string (notify_log, group, "app_name", NULL);

      tmp = g_key_file_get_string (notify_log, group, "summary", NULL);
      markup = g_markup_printf_escaped (format, tmp);
      g_free (tmp);
      summary = gtk_label_new (NULL);
      gtk_label_set_markup (GTK_LABEL (summary), markup);
      gtk_label_set_xalign (GTK_LABEL (summary), 0);
      gtk_label_set_ellipsize (GTK_LABEL (summary), PANGO_ELLIPSIZE_END);
      gtk_label_set_max_width_chars (GTK_LABEL (summary), 40);
      g_free (markup);

      tmp = g_key_file_get_string (notify_log, group, "body", NULL);
      body = gtk_label_new (tmp);
      g_free (tmp);
      gtk_label_set_xalign (GTK_LABEL (body), 0);
      gtk_label_set_ellipsize (GTK_LABEL (body), PANGO_ELLIPSIZE_END);
      gtk_label_set_max_width_chars (GTK_LABEL (body), 40);

      tmp = g_key_file_get_string (notify_log, group, "app_icon", NULL);
      app_icon = gtk_image_new_from_icon_name (tmp, GTK_ICON_SIZE_MENU);
      g_free (tmp);
      gtk_image_set_pixel_size (GTK_IMAGE (app_icon), 16);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (mi), app_icon);
G_GNUC_END_IGNORE_DEPRECATIONS

      tmp = g_key_file_get_string (notify_log, group, "expire-timeout", NULL);
      expire_timeout = gtk_label_new (tmp);
      g_free (tmp);

      grid = gtk_grid_new ();
      gtk_grid_set_column_spacing (GTK_GRID (grid), 6);


      /* Handle icon-only notifications */
      tmp = g_key_file_get_string (notify_log, group, "body", NULL);
      if (g_strcmp0 (tmp, "") == 0) {
        gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (summary), 1, 0, 1, 2);
        markup = g_strdup_printf (tooltip_format_simple, app_name, tooltip_timestamp);
      }
      else {
        gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (summary), 1, 0, 1, 1);
        gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (body), 1, 1, 1, 1);
        markup = g_strdup_printf (tooltip_format, app_name, tooltip_timestamp, tmp);
      }
      g_free (tmp);
      g_free (app_name);

      gtk_widget_set_tooltip_markup (grid, markup);
      g_free (markup);

      gtk_widget_show_all (grid);
      gtk_container_add (GTK_CONTAINER (mi), GTK_WIDGET (grid));
      gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), mi);
      gtk_widget_show (mi);
    }
    g_strfreev (groups);
    g_key_file_free (notify_log);
  }

  /* footer items */
  mi = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
  gtk_widget_show (mi);

  /* checkmenuitem for the do not disturb mode of xfce4-notifyd */
  mi = gtk_check_menu_item_new_with_mnemonic (_("_Do not disturb"));
  xfconf_g_property_bind (notification_plugin->channel, "/do-not-disturb", G_TYPE_BOOLEAN,
                          G_OBJECT (mi), "active");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);

  /* Reset the notification status icon since all items are now read */
  if (xfconf_channel_get_bool (notification_plugin->channel, "/do-not-disturb", TRUE))
    gtk_image_set_from_icon_name (GTK_IMAGE (notification_plugin->image),
                                  "notification-disabled-symbolic", GTK_ICON_SIZE_MENU);
  else
    gtk_image_set_from_icon_name (GTK_IMAGE (notification_plugin->image),
                                  "notification-symbolic", GTK_ICON_SIZE_MENU);
  g_signal_connect (mi, "toggled",
                    G_CALLBACK (dnd_toggled_cb), notification_plugin);
  gtk_widget_show (mi);
}
