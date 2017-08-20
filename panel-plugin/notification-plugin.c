/*  $Id$
 *
 *  Copyright (C) 2017 Simon Steinbeiss <simon@xfce.org>
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
#include <libxfce4util/libxfce4util.h>
#include <libxfce4panel/xfce-panel-plugin.h>

#include "notification-plugin.h"
#include "notification-plugin-dialogs.h"

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



GtkWidget *
notification_plugin_menu_new (/* arguments */)
{
  GtkWidget *menu;
  GtkWidget *mi;

  menu = gtk_menu_new ();
  /* Footer items */
  mi = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
  mi = gtk_menu_item_new_with_label ("This is a notification.");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
  /* Show all the items */
  gtk_widget_show_all (GTK_WIDGET (menu));
  return menu;
}



void
notification_plugin_popup_menu (NotificationPlugin *notification_plugin)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (notification_plugin->button), TRUE);
  gtk_menu_set_screen (GTK_MENU (notification_plugin->menu),
                       gtk_widget_get_screen (notification_plugin->button));
  gtk_menu_popup (GTK_MENU (notification_plugin->menu), NULL, NULL,
                  notification_plugin->menu_position_func, notification_plugin,
                  0, gtk_get_current_event_time ());
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
my_plugin_position_menu (GtkMenu *menu,
                         gint *x,
                         gint *y,
                         gboolean *push_in,
                         NotificationPlugin *notification_plugin)
{
  gboolean above = TRUE;
  gint button_width, button_height;
  GtkRequisition minimum_size;
  GtkRequisition natural_size;
  XfceScreenPosition screen_position;

  g_return_if_fail (XFCE_IS_PANEL_PLUGIN (notification_plugin->plugin));

  screen_position = xfce_panel_plugin_get_screen_position (notification_plugin->plugin);
  gtk_widget_get_size_request (notification_plugin->button, &button_width, &button_height);
  gtk_widget_get_preferred_size (GTK_WIDGET (menu), &minimum_size, &natural_size);
  gdk_window_get_origin (gtk_widget_get_window (GTK_WIDGET (notification_plugin->plugin)), x, y);

  switch (screen_position)
    {
      case XFCE_SCREEN_POSITION_NW_H:
      case XFCE_SCREEN_POSITION_N:
      case XFCE_SCREEN_POSITION_NE_H:
        above = FALSE;
      case XFCE_SCREEN_POSITION_SW_H:
      case XFCE_SCREEN_POSITION_S:
      case XFCE_SCREEN_POSITION_SE_H:
        if (above)
          /* Show menu above */
          *y -= minimum_size.height;
        else
          /* Show menu below */
          *y += button_height;

        if (*x + minimum_size.width > gdk_screen_width ())
          /* Adjust horizontal position */
          *x = gdk_screen_width () - minimum_size.width;

        break;

      default:
        if (*x + button_width + minimum_size.width > gdk_screen_width ())
          /* Show menu on the right */
          *x -= minimum_size.width;
        else
          /* Show menu on the left */
          *x += button_width;

        if (*y + minimum_size.height > gdk_screen_height ())
          /* Adjust vertical position */
          *y = gdk_screen_height () - minimum_size.height;

        break;
    }
}



static NotificationPlugin *
notification_plugin_new (XfcePanelPlugin *panel_plugin)
{
  NotificationPlugin   *notification_plugin;
  GtkOrientation  orientation;
  GtkWidget      *label;

  /* allocate memory for the plugin structure */
  notification_plugin = panel_slice_new0 (NotificationPlugin);

  /* pointer to plugin */
  notification_plugin->plugin = panel_plugin;

  /* read the user settings */
  notification_plugin_read (notification_plugin);

  /* get the current orientation */
  orientation = xfce_panel_plugin_get_orientation (panel_plugin);

  notification_plugin->menu_position_func = (GtkMenuPositionFunc)my_plugin_position_menu;

  /* create some panel widgets */
  xfce_panel_plugin_set_small (panel_plugin, TRUE);
  notification_plugin->button = xfce_panel_create_toggle_button ();
  gtk_widget_set_tooltip_text (GTK_WIDGET (notification_plugin->button), _("Notifications"));
  notification_plugin->image = gtk_image_new_from_icon_name (ICON_NAME, GTK_ICON_SIZE_MENU);
  gtk_container_add (GTK_CONTAINER (notification_plugin->button), notification_plugin->image);
  gtk_container_add (GTK_CONTAINER (panel_plugin), notification_plugin->button);
  gtk_widget_show_all (GTK_WIDGET (notification_plugin->button));
  gtk_widget_set_name (GTK_WIDGET (notification_plugin->button), "xfce4-notification-plugin");

  /* create the menu */
  notification_plugin->menu = notification_plugin_menu_new ();

  g_signal_connect (notification_plugin->button, "button-press-event",
                    G_CALLBACK (cb_button_pressed), notification_plugin);
  g_signal_connect (notification_plugin->menu, "deactivate",
                    G_CALLBACK (cb_menu_deactivate), notification_plugin);

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



static void
notification_plugin_orientation_changed (XfcePanelPlugin *plugin,
                            GtkOrientation   orientation,
                            NotificationPlugin    *notification_plugin)
{
  /* change the orienation of the box */
  gtk_orientable_set_orientation(GTK_ORIENTABLE(notification_plugin->button), orientation);
}



static gboolean
notification_plugin_size_changed (XfcePanelPlugin *plugin,
                     gint             size,
                     NotificationPlugin    *notification_plugin)
{
  GtkOrientation orientation;

  /* get the orientation of the plugin */
  orientation = xfce_panel_plugin_get_orientation (plugin);

  /* set the widget size */
  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    gtk_widget_set_size_request (GTK_WIDGET (plugin), -1, size);
  else
    gtk_widget_set_size_request (GTK_WIDGET (plugin), size, -1);

  /* we handled the orientation */
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

  /* show the panel's right-click menu on this ebox */
  xfce_panel_plugin_add_action_widget (plugin, notification_plugin->button);

  /* connect plugin signals */
  g_signal_connect (G_OBJECT (plugin), "free-data",
                    G_CALLBACK (notification_plugin_free), notification_plugin);

  g_signal_connect (G_OBJECT (plugin), "save",
                    G_CALLBACK (notification_plugin_save), notification_plugin);

  g_signal_connect (G_OBJECT (plugin), "size-changed",
                    G_CALLBACK (notification_plugin_size_changed), notification_plugin);

  g_signal_connect (G_OBJECT (plugin), "orientation-changed",
                    G_CALLBACK (notification_plugin_orientation_changed), notification_plugin);

  /* show the configure menu item and connect signal */
  xfce_panel_plugin_menu_show_configure (plugin);
  g_signal_connect (G_OBJECT (plugin), "configure-plugin",
                    G_CALLBACK (notification_plugin_configure), notification_plugin);

  /* show the about menu item and connect signal */
  xfce_panel_plugin_menu_show_about (plugin);
  g_signal_connect (G_OBJECT (plugin), "about",
                    G_CALLBACK (notification_plugin_about), NULL);
}
