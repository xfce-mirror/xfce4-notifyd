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

#include <string.h>
#include <gtk/gtk.h>

#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4panel/xfce-panel-plugin.h>

#include "notification-plugin.h"
#include "notification-plugin-dialogs.h"

/* the website url */
#define PLUGIN_WEBSITE "https://docs.xfce.org/apps/notifyd/start"



static void
notification_plugin_configure_response (GtkWidget    *dialog,
                           gint          response,
                           NotificationPlugin *notification_plugin)
{
  gboolean result;

  if (response == GTK_RESPONSE_HELP)
    {
      /* show help */
      result = g_spawn_command_line_async ("exo-open --launch WebBrowser " PLUGIN_WEBSITE, NULL);

      if (G_UNLIKELY (result == FALSE))
        g_warning (_("Unable to open the following url: %s"), PLUGIN_WEBSITE);
    }
  else
    {
      /* remove the dialog data from the plugin */
      g_object_set_data (G_OBJECT (notification_plugin->plugin), "dialog", NULL);

      /* unlock the panel menu */
      xfce_panel_plugin_unblock_menu (notification_plugin->plugin);

      /* save the plugin */
      notification_plugin_save (notification_plugin->plugin, notification_plugin);

      /* destroy the properties dialog */
      gtk_widget_destroy (dialog);
    }
}



void
notification_plugin_configure (XfcePanelPlugin *plugin,
                  NotificationPlugin    *notification_plugin)
{
  GtkWidget *dialog;

  /* block the plugin menu */
  xfce_panel_plugin_block_menu (plugin);

  /* create the dialog */
  dialog = xfce_titled_dialog_new_with_buttons (_("Notification Plugin"),
                                                GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (plugin))),
                                                GTK_DIALOG_DESTROY_WITH_PARENT,
                                                "gtk-help", GTK_RESPONSE_HELP,
                                                "gtk-close", GTK_RESPONSE_OK,
                                                NULL);

  /* center dialog on the screen */
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);

  /* set dialog icon */
  gtk_window_set_icon_name (GTK_WINDOW (dialog), ICON_NAME);

  /* link the dialog to the plugin, so we can destroy it when the plugin
   * is closed, but the dialog is still open */
  g_object_set_data (G_OBJECT (plugin), "dialog", dialog);

  /* connect the reponse signal to the dialog */
  g_signal_connect (G_OBJECT (dialog), "response",
                    G_CALLBACK(notification_plugin_configure_response), notification_plugin);

  /* show the entire dialog */
  gtk_widget_show (dialog);
}



void
notification_plugin_about (XfcePanelPlugin *plugin)
{
  const gchar *auth[] =
    {
      "Simon Steinbeiss <simon@xfce.org>",
      NULL
    };
  gtk_show_about_dialog (NULL,
                         "logo-icon-name",         ICON_NAME,
                         "license",                xfce_get_license_text (XFCE_LICENSE_TEXT_GPL),
                         "version",                PACKAGE_VERSION,
                         "program-name",           PACKAGE_NAME,
                         "comments",               _("This is the notification plugin"),
                         "website",                PLUGIN_WEBSITE,
                         "copyright",              _("Copyright \xc2\xa9 2017 Simon Steinbeiß\n"),
                         "authors",                auth,
                         NULL);
}
