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
#include <libxfce4panel/libxfce4panel.h>

#include <common/xfce-notify-log.h>

#include "notification-plugin.h"
#include "notification-plugin-log.h"



static void
notification_plugin_menu_item_activate (GtkWidget      *menuitem,
                                        gpointer        user_data)
{
  NotificationPlugin *notification_plugin = user_data;
  gboolean            muted;

  muted = !gtk_switch_get_active (GTK_SWITCH (notification_plugin->do_not_disturb_switch));
  gtk_switch_set_active (GTK_SWITCH (notification_plugin->do_not_disturb_switch), muted);
  notification_plugin_update_icon (notification_plugin, muted);
}



static void
notification_plugin_settings_activate_cb (GtkMenuItem *menuitem,
                                          gpointer     user_data)
{
  GAppInfo *app_info;
  GError   *error = NULL;

  app_info = g_app_info_create_from_commandline ("xfce4-notifyd-config", "Notification Settings",
                                                 G_APP_INFO_CREATE_NONE, NULL);
  if (!g_app_info_launch (app_info, NULL, NULL, &error)) {
    if (error != NULL) {
      g_warning ("xfce4-notifyd-config could not be launched. %s", error->message);
      g_error_free (error);
    }
  }
}



static void
notification_plugin_menu_clear (GtkWidget *widget, gpointer user_data)
{
  GtkWidget *container = user_data;
  gtk_container_remove (GTK_CONTAINER (container), widget);
}



static void
notification_plugin_clear_log_dialog (GtkWidget *widget, gpointer user_data)
{
  NotificationPlugin* notification_plugin = user_data;
	
	if (xfconf_channel_get_bool (notification_plugin->channel, SETTING_HIDE_CLEAR_PROMPT, FALSE))
	{
	  xfce_notify_log_clear ();
	  return;
	}

  GtkWidget *dialog = xfce_notify_clear_log_dialog ();
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}


void
notification_plugin_menu_populate (NotificationPlugin *notification_plugin)
{
  GtkMenu *menu = GTK_MENU (notification_plugin->menu);
  GtkWidget *mi, *image, *label, *box;
  GKeyFile *notify_log;
  gint i;
  GDateTime *today;
  gchar *timestamp;
  gsize num_groups = 0;
  GtkCallback func = notification_plugin_menu_clear;
  GdkPixbuf *pixbuf = NULL;
  gchar *notify_log_icon_folder;
  gchar *notify_log_icon_path;
  int log_icon_size;
  gboolean state;
  gboolean no_notifications = FALSE;

  today = g_date_time_new_now_local ();
  timestamp = g_date_time_format (today, "%F");

  /* Clean up the list and re-fill it */
  gtk_container_foreach (GTK_CONTAINER (menu), func, menu);

  notify_log = xfce_notify_log_get();
  notify_log_icon_folder = xfce_resource_save_location (XFCE_RESOURCE_CACHE,
                                                          XFCE_NOTIFY_ICON_PATH, TRUE);
  log_icon_size = xfconf_channel_get_int (notification_plugin->channel,
                                          SETTING_LOG_ICON_SIZE, -1);
  if (log_icon_size == -1)
    log_icon_size = DEFAULT_LOG_ICON_SIZE;

  /* switch for the do not disturb mode of xfce4-notifyd */
  mi = gtk_menu_item_new ();
  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  label = gtk_label_new (NULL);
  gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>_Do not disturb</b>"));
#if GTK_CHECK_VERSION (3, 16, 0)
  gtk_label_set_xalign (GTK_LABEL (label), 0);
#else
  gtk_widget_set_halign (label, GTK_ALIGN_START);
#endif
  notification_plugin->do_not_disturb_switch = gtk_switch_new ();
  gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), notification_plugin->do_not_disturb_switch, FALSE, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (mi), box);
  xfconf_g_property_bind (notification_plugin->channel, "/do-not-disturb", G_TYPE_BOOLEAN,
                          G_OBJECT (notification_plugin->do_not_disturb_switch), "active");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
  gtk_widget_show_all (mi);
  /* Reset the notification status icon since all items are now read */
  state = xfconf_channel_get_bool (notification_plugin->channel, "/do-not-disturb", FALSE);
  notification_plugin->new_notifications = FALSE;
  notification_plugin_update_icon (notification_plugin, state);
  g_signal_connect (mi, "activate",
                    G_CALLBACK (notification_plugin_menu_item_activate), notification_plugin);

  /* separator before the log */
  mi = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
  gtk_widget_show (mi);

  if (notify_log) {
    gchar **groups;
    int log_length;
    int log_display_limit;
    int numberof_groups;
    int numberof_notifications_shown = 0;
    gboolean log_only_today;

    groups = g_key_file_get_groups (notify_log, &num_groups);
    /* Substract 1 because the list starts with 0 */
    numberof_groups = GPOINTER_TO_UINT(num_groups) - 1;
    log_display_limit = xfconf_channel_get_int (notification_plugin->channel,
                                                SETTING_LOG_DISPLAY_LIMIT, -1);
    log_only_today = xfconf_channel_get_bool (notification_plugin->channel,
                                              SETTING_LOG_ONLY_TODAY, FALSE);

    if (log_display_limit == -1)
      log_display_limit = DEFAULT_LOG_DISPLAY_LIMIT;
    log_length = numberof_groups - log_display_limit;
    if (log_length < 0)
      log_length = -1;

    /* Check if the menu is going to be empty despite there being a log file, e.g.
       when showing only the notifications of today but the log only contains entries
       from yesterday and before. In this case show the placeholder. */
    if (numberof_groups == -1)
      no_notifications = TRUE;

    /* Notifications are only shown until LOG_DISPLAY_LIMIT is hit */
    for (i = numberof_groups; i > log_length; i--) {
      GtkWidget *grid;
      GtkWidget *summary, *body, *app_icon;
      const gchar *group = groups[i];
      const char *format = "<b>\%s</b>";
      const char *tooltip_format = "<b>\%s</b> - \%s\n\%s";
      const char *tooltip_format_simple = "<b>\%s</b> - \%s";
      char *markup;
      gchar *app_name;
      gchar *tooltip_timestamp = NULL;
      gchar *tmp;
      GDateTime *log_timestamp;
      GDateTime *local_timestamp = NULL;

      /* optionally only show notifications from today */
      if (log_only_today == TRUE) {
        if (g_ascii_strncasecmp (timestamp, group, 10) != 0) {
          no_notifications = TRUE;
          continue;
        }
        else
          numberof_notifications_shown++;
      }
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      mi = gtk_image_menu_item_new ();
G_GNUC_END_IGNORE_DEPRECATIONS

      log_timestamp = g_date_time_new_from_iso8601 (group, NULL);

      if (log_timestamp != NULL)
      {
        local_timestamp = g_date_time_to_local (log_timestamp);
        g_date_time_unref (log_timestamp);
      }

      if (local_timestamp != NULL) {
        tooltip_timestamp = g_date_time_format (local_timestamp, "%c");
        g_date_time_unref (local_timestamp);
      }

      app_name = g_key_file_get_string (notify_log, group, "app_name", NULL);

      tmp = g_key_file_get_string (notify_log, group, "summary", NULL);
      markup = g_markup_printf_escaped (format, tmp);
      g_free (tmp);
      summary = gtk_label_new (NULL);
      gtk_label_set_markup (GTK_LABEL (summary), markup);
#if GTK_CHECK_VERSION (3, 16, 0)
      gtk_label_set_xalign (GTK_LABEL (summary), 0);
#else
      gtk_widget_set_halign (summary, GTK_ALIGN_START);
#endif
      gtk_label_set_ellipsize (GTK_LABEL (summary), PANGO_ELLIPSIZE_END);
      gtk_label_set_max_width_chars (GTK_LABEL (summary), 40);
      g_free (markup);

      tmp = g_key_file_get_string (notify_log, group, "body", NULL);
      body = gtk_label_new (NULL);
      gtk_label_set_markup (GTK_LABEL (body), tmp);
      if (g_strcmp0 (gtk_label_get_text(GTK_LABEL (body)), "") == 0) {
        gchar *tmp1;

        tmp1 = g_markup_escape_text (tmp, -1);
        gtk_label_set_text (GTK_LABEL (body), tmp1);
        g_free (tmp1);
      }
      g_free (tmp);
#if GTK_CHECK_VERSION (3, 16, 0)
      gtk_label_set_xalign (GTK_LABEL (body), 0);
#else
      gtk_widget_set_halign (body, GTK_ALIGN_START);
#endif
      gtk_label_set_ellipsize (GTK_LABEL (body), PANGO_ELLIPSIZE_END);
      gtk_label_set_max_width_chars (GTK_LABEL (body), 40);

      tmp = g_key_file_get_string (notify_log, group, "app_icon", NULL);
      notify_log_icon_path = g_strconcat (notify_log_icon_folder , tmp, ".png", NULL);
      if (g_file_test (notify_log_icon_path, G_FILE_TEST_EXISTS))
      {
          pixbuf = gdk_pixbuf_new_from_file_at_scale (notify_log_icon_path,
                                                      log_icon_size, log_icon_size,
                                                      FALSE, NULL);
          app_icon = gtk_image_new_from_pixbuf (pixbuf);
      }
      else
      {
          app_icon = gtk_image_new_from_icon_name (tmp, GTK_ICON_SIZE_LARGE_TOOLBAR);
      }
      g_free (tmp);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (mi), app_icon);
G_GNUC_END_IGNORE_DEPRECATIONS
      gtk_image_set_pixel_size (GTK_IMAGE (app_icon), log_icon_size);

      grid = gtk_grid_new ();
      gtk_grid_set_column_spacing (GTK_GRID (grid), 6);


      /* Handle icon-only notifications */
      tmp = g_key_file_get_string (notify_log, group, "body", NULL);
      if (g_strcmp0 (tmp, "") == 0) {
        gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (summary), 1, 0, 1, 2);
        if (tooltip_timestamp != NULL) {
          markup = g_strdup_printf (tooltip_format_simple, app_name, tooltip_timestamp);
        }
        else {
          markup = g_strdup_printf (format, app_name);
        }
      }
      else {
        gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (summary), 1, 0, 1, 1);
        gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (body), 1, 1, 1, 1);
        markup = g_strdup_printf (tooltip_format, app_name, tooltip_timestamp, tmp);
      }
      g_free (tmp);
      g_free (app_name);
      g_free (tooltip_timestamp);

      gtk_widget_set_tooltip_markup (mi, markup);
      g_free (markup);

      gtk_widget_show_all (grid);
      gtk_container_add (GTK_CONTAINER (mi), GTK_WIDGET (grid));
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
      gtk_widget_show (mi);
    }
    g_strfreev (groups);
    g_key_file_free (notify_log);
    if (numberof_notifications_shown > 0)
      no_notifications = FALSE;
  }

  g_free (timestamp);
  g_date_time_unref (today);

  /* Show a placeholder label when there are no notifications */
  if (!notify_log ||
      no_notifications) {
    GtkStyleContext *context;
    GtkBorder padding;
    mi = gtk_menu_item_new ();
    label = gtk_label_new (_("No notifications"));
    gtk_widget_set_sensitive (mi, FALSE);
    gtk_container_add (GTK_CONTAINER (mi), label);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
    gtk_widget_show_all (mi);
    /* Center the text and add top and bottom padding */
    context = gtk_widget_get_style_context (GTK_WIDGET (mi));
    gtk_style_context_get_padding (context, gtk_widget_get_state_flags (GTK_WIDGET (mi)), &padding);
    gtk_widget_set_margin_end (label, log_icon_size + padding.left);
    gtk_widget_set_margin_top (label, padding.top * 2);
    gtk_widget_set_margin_bottom (label, padding.top * 2);
  }

  /* footer items */
  mi = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
  gtk_widget_show (mi);

  /* checkmenuitem for the do not disturb mode of xfce4-notifyd */
  image = gtk_image_new_from_icon_name ("edit-clear-symbolic", GTK_ICON_SIZE_MENU);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  mi = gtk_image_menu_item_new_with_mnemonic (_("_Clear log"));
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (mi), image);
G_GNUC_END_IGNORE_DEPRECATIONS
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
  gtk_widget_show (mi);
  g_signal_connect (mi, "activate", G_CALLBACK (notification_plugin_clear_log_dialog),
                    notification_plugin);

  mi = gtk_menu_item_new_with_mnemonic (_("_Notification settings…"));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
  gtk_widget_show (mi);
  g_signal_connect (mi, "activate", G_CALLBACK (notification_plugin_settings_activate_cb),
                    notification_plugin);
}
