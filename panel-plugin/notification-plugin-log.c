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

#include <common/xfce-notify-common.h>
#include <common/xfce-notify-log.h>
#include <common/xfce-notify-log-util.h>

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
  GtkWidget *dialog;
	
  if (xfconf_channel_get_bool (notification_plugin->channel, SETTING_HIDE_CLEAR_PROMPT, FALSE)) {
    xfce_notify_log_clear(notification_plugin->log);
  } else {
    dialog = xfce_notify_clear_log_dialog(notification_plugin->log);
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
  }
}


static void
notification_plugin_mark_all_read(GtkWidget *widget, NotificationPlugin *notification_plugin) {
  xfce_notify_log_mark_all_read(notification_plugin->log);
}


void
notification_plugin_menu_populate (NotificationPlugin *notification_plugin)
{
  GtkMenu *menu = GTK_MENU (notification_plugin->menu);
  GtkWidget *mi, *image, *label, *box;
  GDateTime *today;
  gint today_year, today_day;
  GtkCallback func = notification_plugin_menu_clear;
  gchar *notify_log_icon_folder;
  int log_icon_size;
  gboolean state;
  XfceDateTimeFormat dt_format;
  gchar *custom_dt_format;
  const gchar *show_in_menu;
  gboolean only_unread;
  const gchar *after_menu_shown;
  gboolean mark_shown_read;
  gboolean mark_all_read;
  gboolean no_notifications = FALSE;
  gint scale_factor = gtk_widget_get_scale_factor(notification_plugin->button);

  today = g_date_time_new_now_local();
  today_year = g_date_time_get_year(today);
  today_day = g_date_time_get_day_of_year(today);

  /* Clean up the list and re-fill it */
  gtk_container_foreach (GTK_CONTAINER (menu), func, menu);

  notify_log_icon_folder = xfce_resource_save_location (XFCE_RESOURCE_CACHE,
                                                        XFCE_NOTIFY_ICON_PATH, TRUE);
  log_icon_size = xfconf_channel_get_int (notification_plugin->channel,
                                          SETTING_LOG_ICON_SIZE, -1);
  if (log_icon_size == -1)
    log_icon_size = DEFAULT_LOG_ICON_SIZE;
  dt_format = xfconf_channel_get_int(notification_plugin->channel, DATETIME_FORMAT_PROP, XFCE_DATE_TIME_FORMAT_LOCALE);
  custom_dt_format = xfconf_channel_get_string(notification_plugin->channel, DATETIME_CUSTOM_FORMAT_PROP, DATETIME_CUSTOM_FORMAT_DEFAULT);

  show_in_menu = xfconf_channel_get_string(notification_plugin->channel, SETTING_SHOW_IN_MENU, VALUE_SHOW_ALL);
  only_unread = g_strcmp0(show_in_menu, VALUE_SHOW_UNREAD) == 0;

  after_menu_shown = xfconf_channel_get_string(notification_plugin->channel, SETTING_AFTER_MENU_SHOWN, VALUE_MARK_ALL_READ);
  mark_shown_read = g_strcmp0(after_menu_shown, VALUE_MARK_SHOWN_READ) == 0;
  mark_all_read = g_strcmp0(after_menu_shown, VALUE_MARK_ALL_READ) == 0;

  /* switch for the do not disturb mode of xfce4-notifyd */
  mi = gtk_menu_item_new ();
  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  label = gtk_label_new (NULL);
  gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("<b>_Do not disturb</b>"));
  gtk_label_set_xalign (GTK_LABEL (label), 0);
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

  if (notification_plugin->log != NULL) {
    GList *entries;
    int log_display_limit;
    int numberof_notifications_shown = 0;
    gboolean log_only_today;

    log_display_limit = xfconf_channel_get_int (notification_plugin->channel,
                                                SETTING_LOG_DISPLAY_LIMIT, -1);
    log_only_today = xfconf_channel_get_bool (notification_plugin->channel,
                                              SETTING_LOG_ONLY_TODAY, FALSE);
    if (log_display_limit == -1)
      log_display_limit = DEFAULT_LOG_DISPLAY_LIMIT;

    if (only_unread) {
        entries = xfce_notify_log_read_unread(notification_plugin->log, NULL, log_display_limit);
    } else {
        entries = xfce_notify_log_read(notification_plugin->log, NULL, log_display_limit);
    }

    /* Check if the menu is going to be empty despite there being a log file, e.g.
       when showing only the notifications of today but the log only contains entries
       from yesterday and before. In this case show the placeholder. */
    if (entries == NULL)
      no_notifications = TRUE;

    for (GList *l = entries; l != NULL; l = l->next) {
      XfceNotifyLogEntry *entry = l->data;
      GtkWidget *hbox;
      GtkWidget *timestamp, *summary, *body = NULL, *app_icon = NULL;
      const gchar *app_name;
      gchar *timestamp_text;
      gchar *summary_text;
      gchar *body_text;
      cairo_surface_t *icon;
      gchar *tooltip_timestamp_text;
      gchar *tooltip_text;

      /* optionally only show notifications from today */
      if (log_only_today == TRUE) {
        GDateTime *entry_local = g_date_time_to_local(entry->timestamp);
        gint entry_year = g_date_time_get_year(entry_local);
        gint entry_day = g_date_time_get_day_of_year(entry_local);
        g_date_time_unref(entry_local);

        if (entry_year != today_year || entry_day != today_day) {
          no_notifications = TRUE;
          continue;
        } else {
          numberof_notifications_shown++;
        }
      }

      app_name = entry->app_name != NULL ? entry->app_name : entry->app_id;
      timestamp_text = notify_log_format_timestamp(entry->timestamp, dt_format, custom_dt_format);
      summary_text = notify_log_format_summary(entry->summary);
      body_text = notify_log_format_body(entry->body);
      icon = notify_log_load_icon(notify_log_icon_folder, entry->icon_id, entry->app_id, log_icon_size, scale_factor);
      tooltip_timestamp_text = notify_log_format_timestamp(entry->timestamp, XFCE_DATE_TIME_FORMAT_LOCALE, NULL);
      tooltip_text = notify_log_format_tooltip(app_name, tooltip_timestamp_text, body_text);

      summary = g_object_new(GTK_TYPE_LABEL,
                             "use-markup", TRUE,
                             "label", summary_text,
                             "max-width-chars", 30,
                             "ellipsize", PANGO_ELLIPSIZE_END,
                             "xalign", 0.0,
                             NULL);
      timestamp = g_object_new(GTK_TYPE_LABEL,
                               "label", timestamp_text,
                               "xalign", 1.0,
                               NULL);
      if (body_text != NULL) {
        body = g_object_new(GTK_TYPE_LABEL,
                            "use-markup", TRUE,
                            "label", body_text,
                            "max-width-chars", 60,
                            "ellipsize", PANGO_ELLIPSIZE_END,
                            "xalign", 0.0,
                            NULL);
      }
      if (icon != NULL) {
        app_icon = gtk_image_new_from_surface(icon);
      }

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      mi = gtk_image_menu_item_new ();
      gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi), app_icon);
G_GNUC_END_IGNORE_DEPRECATIONS
      gtk_widget_set_tooltip_markup(mi, tooltip_text);

      hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
      gtk_widget_show(hbox);
      gtk_box_pack_start(GTK_BOX(hbox), summary, TRUE, TRUE, 0);
      gtk_box_pack_end(GTK_BOX(hbox), timestamp, FALSE, FALSE, 0);

      if (body == NULL) {
        gtk_container_add(GTK_CONTAINER(mi), hbox);
      } else {
        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_show(vbox);
        gtk_container_add(GTK_CONTAINER(mi), vbox);

        gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), body, FALSE, FALSE, 0);
      }

      gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
      gtk_widget_show_all(mi);

      if (mark_shown_read && !entry->is_read) {
        xfce_notify_log_mark_read(notification_plugin->log, entry->id);
      }

      g_free(timestamp_text);
      g_free(summary_text);
      g_free(body_text);
      g_free(tooltip_timestamp_text);
      g_free(tooltip_text);
      if (icon != NULL) {
          cairo_surface_destroy(icon);
      }
      xfce_notify_log_entry_unref(entry);
    }

    g_list_free(entries);

    if (numberof_notifications_shown > 0)
      no_notifications = FALSE;
  }

  g_date_time_unref (today);
  g_free(custom_dt_format);

  if (notification_plugin->log != NULL && mark_all_read) {
    xfce_notify_log_mark_all_read(notification_plugin->log);
  }

  /* Show a placeholder label when there are no notifications */
  if (notification_plugin->log == NULL || no_notifications) {
    GtkStyleContext *context;
    GtkBorder padding;
    mi = gtk_menu_item_new ();
    if (notification_plugin->log == NULL) {
      label = gtk_label_new(_("Unable to open notification log"));
    } else if (only_unread) {
      label = gtk_label_new(_("No unread notifications"));
    } else {
      label = gtk_label_new (_("No notifications"));
    }
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
  gtk_widget_set_sensitive(mi, notification_plugin->log != NULL);
  gtk_widget_show (mi);
  g_signal_connect (mi, "activate", G_CALLBACK (notification_plugin_clear_log_dialog),
                    notification_plugin);

  image = gtk_image_new_from_icon_name("notification-new-symbolic", GTK_ICON_SIZE_MENU);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  mi = gtk_image_menu_item_new_with_mnemonic(_("_Mark all read"));
  gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi), image);
G_GNUC_END_IGNORE_DEPRECATIONS
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
  gtk_widget_set_sensitive(mi, notification_plugin->log != NULL && xfce_notify_log_count_unread_messages(notification_plugin->log) > 0);
  gtk_widget_show(mi);
  g_signal_connect(mi, "activate",
                   G_CALLBACK(notification_plugin_mark_all_read), notification_plugin);

  mi = gtk_menu_item_new_with_mnemonic (_("_Notification settings…"));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
  gtk_widget_show (mi);
  g_signal_connect (mi, "activate", G_CALLBACK (notification_plugin_settings_activate_cb),
                    notification_plugin);
}
