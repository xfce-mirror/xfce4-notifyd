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

#include <gtk/gtk.h>
#include <xfconf/xfconf.h>
#include <libxfce4util/libxfce4util.h>
#include <libxfce4panel/libxfce4panel.h>

#include "notification-plugin.h"
#include "notification-plugin-log.h"
#include "notification-plugin-dialogs.h"

/* prototypes */
static void
notification_plugin_construct (XfcePanelPlugin *panel_plugin);

/* register the plugin */
XFCE_PANEL_PLUGIN_REGISTER (notification_plugin_construct);

GtkWidget *notification_plugin_menu_new   (NotificationPlugin *notification_plugin);
void       notification_plugin_popup_menu (NotificationPlugin *notification_plugin);



GtkWidget *
notification_plugin_menu_new (NotificationPlugin *notification_plugin)
{
  GtkWidget *menu;

  menu = gtk_menu_new ();
  /* connect signal on show to update the items */
  g_signal_connect_swapped (menu, "show", G_CALLBACK (notification_plugin_menu_populate),
                            notification_plugin);

  /* Show all the items */
  gtk_widget_show_all (GTK_WIDGET (menu));
  return menu;
}



void
notification_plugin_popup_menu (NotificationPlugin *notification_plugin)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (notification_plugin->button), TRUE);
#if GTK_CHECK_VERSION (3, 22, 0)
  gtk_menu_popup_at_widget (GTK_MENU (notification_plugin->menu),
                            notification_plugin->button,
                            xfce_panel_plugin_get_orientation (notification_plugin->plugin) == GTK_ORIENTATION_VERTICAL
                            ? GDK_GRAVITY_NORTH_EAST : GDK_GRAVITY_SOUTH_WEST,
                            GDK_GRAVITY_NORTH_WEST,
                            NULL);
#else
  gtk_menu_popup (GTK_MENU (notification_plugin->menu), NULL, NULL,
                  xfce_panel_plugin_position_menu, notification_plugin, 0, 0);
#endif
  xfce_panel_plugin_register_menu (notification_plugin->plugin,
                                   GTK_MENU (notification_plugin->menu));
}



static gboolean
cb_button_pressed (GtkButton *button,
                   GdkEventButton *event,
                   NotificationPlugin *notification_plugin)
{
  if (event->button == 1 && !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
    {
      notification_plugin_popup_menu (notification_plugin);
      return TRUE;
    }

  if (event->button == 2)
    {
      gboolean state = xfconf_channel_get_bool (notification_plugin->channel, "/do-not-disturb", FALSE);
      xfconf_channel_set_bool (notification_plugin->channel, "/do-not-disturb", !state);
      return TRUE;
    }

  return FALSE;
}



static void
cb_menu_deactivate (GtkMenuShell *menu,
                    NotificationPlugin *notification_plugin)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (notification_plugin->button), FALSE);
}



static gboolean
cb_menu_size_allocate_next (NotificationPlugin *notification_plugin)
{
  gtk_menu_reposition (GTK_MENU (notification_plugin->menu));
  notification_plugin->menu_size_allocate_next_handler = 0;

  return G_SOURCE_REMOVE;
}



static void
cb_menu_size_allocate (GtkWidget          *menu,
                       GdkRectangle       *allocation,
                       NotificationPlugin *notification_plugin)
{
  if (notification_plugin->menu_size_allocate_next_handler != 0)
    g_source_remove (notification_plugin->menu_size_allocate_next_handler);

  /* defer gtk_menu_reposition call since it may not work in size event handler */
  notification_plugin->menu_size_allocate_next_handler =
    g_idle_add ((GSourceFunc)cb_menu_size_allocate_next, notification_plugin);
}



void
notification_plugin_update_icon (NotificationPlugin *notification_plugin,
                                 gboolean state)
{
  if (state && !notification_plugin->new_notifications)
    gtk_image_set_from_icon_name (GTK_IMAGE (notification_plugin->image),
                                  "notification-disabled-symbolic", GTK_ICON_SIZE_MENU);
  else if (!state && !notification_plugin->new_notifications)
    gtk_image_set_from_icon_name (GTK_IMAGE (notification_plugin->image),
                                  "notification-symbolic", GTK_ICON_SIZE_MENU);
  else if (state && notification_plugin->new_notifications)
    gtk_image_set_from_icon_name (GTK_IMAGE (notification_plugin->image),
                                  "notification-disabled-new-symbolic", GTK_ICON_SIZE_MENU);
  else if (!state && notification_plugin->new_notifications)
    gtk_image_set_from_icon_name (GTK_IMAGE (notification_plugin->image),
                                  "notification-new-symbolic", GTK_ICON_SIZE_MENU);
}



static void
notification_plugin_dnd_updated (XfconfChannel *channel,
                                 const gchar *property,
                                 const GValue *value,
                                 gpointer user_data)
{
  NotificationPlugin *notification_plugin = user_data;
  gboolean state;

  state = xfconf_channel_get_bool (notification_plugin->channel, "/do-not-disturb", FALSE);
  notification_plugin_update_icon (notification_plugin, state);
}



static void
notification_plugin_log_file_changed (GFileMonitor     *monitor,
                                       GFile            *file,
                                       GFile            *other_file,
                                       GFileMonitorEvent event_type,
                                       gpointer          user_data)
{
  NotificationPlugin    *notification_plugin = user_data;
  gboolean state;

  state = xfconf_channel_get_bool (notification_plugin->channel, "/do-not-disturb", FALSE);
  /* If the log gets cleared, the file gets deleted so make sure not to indicate that
     there are new notifications */
  if (event_type == G_FILE_MONITOR_EVENT_DELETED)
    notification_plugin->new_notifications = FALSE;
  else if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
    notification_plugin->new_notifications = TRUE;

  notification_plugin_update_icon (notification_plugin, state);
}



static NotificationPlugin *
notification_plugin_new (XfcePanelPlugin *panel_plugin)
{
  NotificationPlugin    *notification_plugin;
  GFile                 *log_file;
  GFileMonitor          *log_file_monitor;
  gchar                 *notify_log_path = NULL;
  gboolean               state;

  /* Allocate memory for the plugin structure */
  notification_plugin = g_slice_new0 (NotificationPlugin);
  notification_plugin->plugin = panel_plugin;

  /* Initialize xfconf */
  xfconf_init (NULL);
  notification_plugin->channel = xfconf_channel_new ("xfce4-notifyd");

  /* As the plugin is starting up we presume there are no new notifications */
  notification_plugin->new_notifications = FALSE;

  /* Create the panel widgets (image-button) */
  xfce_panel_plugin_set_small (panel_plugin, TRUE);
  notification_plugin->button = xfce_panel_create_toggle_button ();
  gtk_widget_set_tooltip_text (GTK_WIDGET (notification_plugin->button), _("Notifications"));
  notification_plugin->image = gtk_image_new ();
  state = xfconf_channel_get_bool (notification_plugin->channel, "/do-not-disturb", FALSE);
  notification_plugin_update_icon (notification_plugin, state);

  gtk_container_add (GTK_CONTAINER (notification_plugin->button), notification_plugin->image);
  gtk_container_add (GTK_CONTAINER (panel_plugin), notification_plugin->button);
  gtk_widget_show_all (GTK_WIDGET (notification_plugin->button));
  gtk_widget_set_name (GTK_WIDGET (notification_plugin->button), "xfce4-notification-plugin");

  /* Create the menu */
  notification_plugin->menu = notification_plugin_menu_new (notification_plugin);
  gtk_menu_attach_to_widget (GTK_MENU (notification_plugin->menu), notification_plugin->button, NULL);
  gtk_widget_set_name (GTK_WIDGET (notification_plugin->menu), "xfce4-notification-plugin-menu");

  g_signal_connect (notification_plugin->button, "button-press-event",
                    G_CALLBACK (cb_button_pressed), notification_plugin);
  g_signal_connect (notification_plugin->menu, "deactivate",
                    G_CALLBACK (cb_menu_deactivate), notification_plugin);
  g_signal_connect (notification_plugin->menu, "size-allocate",
                    G_CALLBACK (cb_menu_size_allocate), notification_plugin);

  /* Start monitoring the log file for changes */
  notify_log_path = xfce_resource_lookup (XFCE_RESOURCE_CACHE, XFCE_NOTIFY_LOG_FILE);
  log_file = g_file_new_for_path (notify_log_path);
  log_file_monitor = g_file_monitor_file (log_file, G_FILE_MONITOR_NONE, NULL, NULL);
  g_signal_connect (log_file_monitor, "changed",
                    G_CALLBACK (notification_plugin_log_file_changed), notification_plugin);

  /* Start monitoring the "do not disturb" setting in xfconf */
  g_signal_connect (G_OBJECT (notification_plugin->channel), "property-changed::" "/do-not-disturb",
                    G_CALLBACK (notification_plugin_dnd_updated), notification_plugin);

  return notification_plugin;
}



static void
notification_plugin_free (XfcePanelPlugin *plugin,
                          NotificationPlugin    *notification_plugin)
{
  GtkWidget *dialog;

  /* check if the dialog is still open. if so, destroy it */
  dialog = g_object_get_data (G_OBJECT (plugin), "dialog");
  if (G_UNLIKELY (dialog != NULL))
    gtk_widget_destroy (dialog);

  /* destroy the panel widgets */
  gtk_widget_destroy (notification_plugin->button);

  /* remove deferred size allocation handler */
  if (notification_plugin->menu_size_allocate_next_handler != 0)
    g_source_remove (notification_plugin->menu_size_allocate_next_handler);

  /* free the plugin structure */
  g_slice_free (NotificationPlugin, notification_plugin);
}



static gboolean
notification_plugin_size_changed (XfcePanelPlugin       *plugin,
                                  gint                   size,
                                  NotificationPlugin    *notification_plugin)
{
#if !LIBXFCE4PANEL_CHECK_VERSION (4, 13, 0)
  GtkStyleContext *context;
  GtkBorder padding, border;
  gint width;
  gint xthickness;
  gint ythickness;
#endif
  gint icon_size;

  size /= xfce_panel_plugin_get_nrows (notification_plugin->plugin);
  gtk_widget_set_size_request (GTK_WIDGET (notification_plugin->button), size, size);
#if LIBXFCE4PANEL_CHECK_VERSION (4,13,0)
  icon_size = xfce_panel_plugin_get_icon_size (XFCE_PANEL_PLUGIN (plugin));
#else
  /* Calculate the size of the widget because the theme can override it */
  context = gtk_widget_get_style_context (GTK_WIDGET (notification_plugin->button));
  gtk_style_context_get_padding (context, gtk_widget_get_state_flags (GTK_WIDGET (notification_plugin->button)), &padding);
  gtk_style_context_get_border (context, gtk_widget_get_state_flags (GTK_WIDGET (notification_plugin->button)), &border);
  xthickness = padding.left + padding.right + border.left + border.right;
  ythickness = padding.top + padding.bottom + border.top + border.bottom;

  /* Calculate the size of the space left for the icon */
  width = size - 2 * MAX (xthickness, ythickness);

  /* Since symbolic icons are usually only provided in 16px we
   * try to be clever and use size steps */
  if (width <= 21)
    icon_size = 16;
  else if (width >=22 && width <= 29)
    icon_size = 24;
  else if (width >= 30 && width <= 40)
    icon_size = 32;
  else
    icon_size = width;
#endif

  gtk_image_set_pixel_size (GTK_IMAGE (notification_plugin->image), icon_size);

  return TRUE;
}



static void
notification_plugin_construct (XfcePanelPlugin *plugin)
{
  NotificationPlugin *notification_plugin;

  /* setup transation domain */
  xfce_textdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");

  /* create the plugin */
  notification_plugin = notification_plugin_new (plugin);

  /* add the button to the panel */
  gtk_container_add (GTK_CONTAINER (plugin), notification_plugin->button);
  /* show the panel's right-click menu */
  xfce_panel_plugin_add_action_widget (plugin, notification_plugin->button);

  /* connect plugin signals */
  g_signal_connect (G_OBJECT (plugin), "free-data",
                    G_CALLBACK (notification_plugin_free), notification_plugin);

  g_signal_connect (G_OBJECT (plugin), "size-changed",
                    G_CALLBACK (notification_plugin_size_changed), notification_plugin);

  /* show the configure menu item and connect signal */
  xfce_panel_plugin_menu_show_configure (plugin);
  g_signal_connect (G_OBJECT (plugin), "configure-plugin",
                    G_CALLBACK (notification_plugin_configure), notification_plugin);

  /* show the about menu item and connect signal */
  xfce_panel_plugin_menu_show_about (plugin);
  g_signal_connect (G_OBJECT (plugin), "about",
                    G_CALLBACK (notification_plugin_about), NULL);
}
