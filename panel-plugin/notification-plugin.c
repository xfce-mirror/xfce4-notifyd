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
#include <libxfce4panel/xfce-panel-plugin.h>

#include "notification-plugin.h"
#include "notification-plugin-dialogs.h"
#include "notification-plugin-log.h"

/* default settings */
#define DEFAULT_SETTING1 NULL
#define DEFAULT_SETTING2 1
#define DEFAULT_SETTING3 FALSE



/* prototypes */
static void
notification_plugin_construct (XfcePanelPlugin *panel_plugin);


/* register the plugin */
XFCE_PANEL_PLUGIN_REGISTER (notification_plugin_construct);



void
notification_plugin_save (XfcePanelPlugin *plugin,
                          NotificationPlugin    *notification_plugin)
{
  XfceRc *rc;
  gchar  *file;

  /* get the config file location */
  file = xfce_panel_plugin_save_location (plugin, TRUE);

  if (G_UNLIKELY (file == NULL))
    {
       DBG ("Failed to open config file");
       return;
    }

  /* open the config file, read/write */
  rc = xfce_rc_simple_open (file, FALSE);
  g_free (file);

  if (G_LIKELY (rc != NULL))
    {
      /* save the settings */
      DBG(".");
      if (notification_plugin->setting1)
        xfce_rc_write_entry    (rc, "setting1", notification_plugin->setting1);

      xfce_rc_write_int_entry  (rc, "setting2", notification_plugin->setting2);
      xfce_rc_write_bool_entry (rc, "setting3", notification_plugin->setting3);

      /* close the rc file */
      xfce_rc_close (rc);
    }
}



static void
notification_plugin_read (NotificationPlugin *notification_plugin)
{
  XfceRc      *rc;
  gchar       *file;
  const gchar *value;

  /* get the plugin config file location */
  file = xfce_panel_plugin_save_location (notification_plugin->plugin, TRUE);

  if (G_LIKELY (file != NULL))
    {
      /* open the config file, readonly */
      rc = xfce_rc_simple_open (file, TRUE);

      /* cleanup */
      g_free (file);

      if (G_LIKELY (rc != NULL))
        {
          /* read the settings */
          value = xfce_rc_read_entry (rc, "setting1", DEFAULT_SETTING1);
          notification_plugin->setting1 = g_strdup (value);

          notification_plugin->setting2 = xfce_rc_read_int_entry (rc, "setting2", DEFAULT_SETTING2);
          notification_plugin->setting3 = xfce_rc_read_bool_entry (rc, "setting3", DEFAULT_SETTING3);

          /* cleanup */
          xfce_rc_close (rc);

          /* leave the function, everything went well */
          return;
        }
    }

  /* something went wrong, apply default values */
  DBG ("Applying default settings");

  notification_plugin->setting1 = g_strdup (DEFAULT_SETTING1);
  notification_plugin->setting2 = DEFAULT_SETTING2;
  notification_plugin->setting3 = DEFAULT_SETTING3;
}



void
dnd_toggled_cb (GtkCheckMenuItem *checkmenuitem,
                gpointer          user_data)
{
  NotificationPlugin *notification_plugin = user_data;

  if (gtk_check_menu_item_get_active (checkmenuitem))
    gtk_image_set_from_icon_name (GTK_IMAGE (notification_plugin->image),
                                  "notification-disabled-symbolic", GTK_ICON_SIZE_MENU);
  else
    gtk_image_set_from_icon_name (GTK_IMAGE (notification_plugin->image),
                                  "notification-symbolic", GTK_ICON_SIZE_MENU);
}



GtkWidget *
notification_plugin_menu_new (NotificationPlugin *notification_plugin)
{
  GtkWidget *mi;
  GtkWidget *label;

  notification_plugin->menu = gtk_menu_new ();
  /* connect signal on show to update the items */
  g_signal_connect_swapped (notification_plugin->menu, "show", G_CALLBACK (notification_plugin_menu_populate),
                            notification_plugin);

  /* Show all the items */
  gtk_widget_show_all (GTK_WIDGET (notification_plugin->menu));
  return notification_plugin->menu;
}



void
notification_plugin_popup_menu (NotificationPlugin *notification_plugin)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (notification_plugin->button), TRUE);
  gtk_menu_popup_at_widget (GTK_MENU (notification_plugin->menu), notification_plugin->button,
                            GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
  xfce_panel_plugin_register_menu (notification_plugin->plugin,
                                   GTK_MENU (notification_plugin->menu));
}



static gboolean
cb_button_pressed (GtkButton *button,
                   GdkEventButton *event,
                   NotificationPlugin *notification_plugin)
{
  if (event->button != 1 && !(event->state & GDK_CONTROL_MASK))
    return FALSE;
  if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
    notification_plugin_popup_menu (notification_plugin);

  return TRUE;
}



static void
cb_menu_deactivate (GtkMenuShell *menu,
                    NotificationPlugin *notification_plugin)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (notification_plugin->button), FALSE);
}



static void
notification_plugin_log_file_changed (GFileMonitor     *monitor,
                                       GFile            *file,
                                       GFile            *other_file,
                                       GFileMonitorEvent event_type,
                                       gpointer          user_data)
{
  NotificationPlugin    *notification_plugin = user_data;

  if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
  {
    gtk_image_set_from_icon_name (GTK_IMAGE (notification_plugin->image),
                                  "notification-new-symbolic", GTK_ICON_SIZE_MENU);
  }
}



static NotificationPlugin *
notification_plugin_new (XfcePanelPlugin *panel_plugin)
{
  NotificationPlugin    *notification_plugin;
  GFile                 *log_file;
  GFileMonitor          *log_file_monitor;
  gchar                 *notify_log_path = NULL;

  /* allocate memory for the plugin structure */
  notification_plugin = panel_slice_new0 (NotificationPlugin);
  notification_plugin->plugin = panel_plugin;

  /* read the user settings */
  notification_plugin_read (notification_plugin);

  /* xfconf */
  xfconf_init (NULL);
  notification_plugin->channel = xfconf_channel_new ("xfce4-notifyd");

  /* create some panel widgets */
  xfce_panel_plugin_set_small (panel_plugin, TRUE);
  notification_plugin->button = xfce_panel_create_toggle_button ();
  gtk_widget_set_tooltip_text (GTK_WIDGET (notification_plugin->button), _("Notifications"));
  notification_plugin->image = gtk_image_new_from_icon_name ("notification-symbolic", GTK_ICON_SIZE_MENU);
  gtk_container_add (GTK_CONTAINER (notification_plugin->button), notification_plugin->image);
  gtk_container_add (GTK_CONTAINER (panel_plugin), notification_plugin->button);
  gtk_widget_show_all (GTK_WIDGET (notification_plugin->button));
  gtk_widget_set_name (GTK_WIDGET (notification_plugin->button), "xfce4-notification-plugin");

  /* create the menu */
  notification_plugin->menu = notification_plugin_menu_new (notification_plugin);
  gtk_widget_set_name (GTK_WIDGET (notification_plugin->menu), "xfce4-notification-plugin-menu");

  g_signal_connect (notification_plugin->button, "button-press-event",
                    G_CALLBACK (cb_button_pressed), notification_plugin);
  g_signal_connect (notification_plugin->menu, "deactivate",
                    G_CALLBACK (cb_menu_deactivate), notification_plugin);

  /* start monitoring the log file for changes */
  notify_log_path = xfce_resource_lookup (XFCE_RESOURCE_CACHE, XFCE_NOTIFY_LOG_FILE);
  log_file = g_file_new_for_path (notify_log_path);
  log_file_monitor = g_file_monitor_file (log_file, G_FILE_MONITOR_NONE, NULL, NULL);
  g_signal_connect (log_file_monitor, "changed",
                    G_CALLBACK (notification_plugin_log_file_changed), notification_plugin);

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

  /* cleanup the settings */
  if (G_LIKELY (notification_plugin->setting1 != NULL))
    g_free (notification_plugin->setting1);

  /* free the plugin structure */
  panel_slice_free (NotificationPlugin, notification_plugin);
}



static gboolean
notification_plugin_size_changed (XfcePanelPlugin       *plugin,
                                  gint                   size,
                                  NotificationPlugin    *notification_plugin)
{
  gint icon_size;

  size /= xfce_panel_plugin_get_nrows (notification_plugin->plugin);
  gtk_widget_set_size_request (GTK_WIDGET (notification_plugin->button), size, size);
#if LIBXFCE4PANEL_CHECK_VERSION (4,13,0)
  icon_size = xfce_panel_plugin_get_icon_size (XFCE_PANEL_PLUGIN (plugin));
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

  g_signal_connect (G_OBJECT (plugin), "save",
                    G_CALLBACK (notification_plugin_save), notification_plugin);

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
