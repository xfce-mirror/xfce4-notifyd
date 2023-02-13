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

#include "common/xfce-notify-common.h"
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

static gboolean notification_plugin_size_changed (XfcePanelPlugin *plugin,
                                                  gint size,
                                                  NotificationPlugin *notification_plugin);


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
  gtk_menu_popup_at_widget (GTK_MENU (notification_plugin->menu),
                            notification_plugin->button,
                            xfce_panel_plugin_get_orientation (notification_plugin->plugin) == GTK_ORIENTATION_VERTICAL
                            ? GDK_GRAVITY_NORTH_EAST : GDK_GRAVITY_SOUTH_WEST,
                            GDK_GRAVITY_NORTH_WEST,
                            NULL);
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
  gtk_widget_set_visible(notification_plugin->button,
                         !notification_plugin->hide_on_read
                         || notification_plugin->new_notifications);
}



static gboolean
cb_menu_size_allocate_next (gpointer user_data)
{
  NotificationPlugin *notification_plugin = user_data;

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
    g_idle_add (cb_menu_size_allocate_next, notification_plugin);
}



static void
cb_hide_on_read_changed(XfconfChannel *channel,
                        const gchar *property_name,
                        const GValue *value,
                        NotificationPlugin *notification_plugin)
{
  if (G_VALUE_HOLDS_BOOLEAN(value)) {
    notification_plugin->hide_on_read = g_value_get_boolean(value);
    gtk_widget_set_visible(notification_plugin->button,
                           !notification_plugin->hide_on_read
                           || notification_plugin->new_notifications
                           || gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(notification_plugin->button)));
  }
}



void
notification_plugin_update_icon(NotificationPlugin *notification_plugin) {
  GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
  GtkStyleContext *style_context = gtk_widget_get_style_context(notification_plugin->image);
  GIcon *base_icon;
  GtkIconInfo *icon_info;
  gint scale_factor;
  gboolean dnd_enabled; 

  dnd_enabled = xfconf_channel_get_bool(notification_plugin->channel, DND_ENABLED_PROP, FALSE);
  if (dnd_enabled) {
    base_icon = g_themed_icon_new_with_default_fallbacks("notification-disabled-symbolic");
    g_themed_icon_append_name(G_THEMED_ICON(base_icon), "notifications-disabled-symbolic");
  } else {
    base_icon = g_themed_icon_new_with_default_fallbacks("notification-symbolic");
    g_themed_icon_append_name(G_THEMED_ICON(base_icon), "notifications-symbolic");
  }

  scale_factor = gtk_widget_get_scale_factor(notification_plugin->button);
  icon_info = gtk_icon_theme_lookup_by_gicon_for_scale(icon_theme,
                                                       base_icon,
                                                       notification_plugin->icon_size,
                                                       scale_factor,
                                                       GTK_ICON_LOOKUP_FORCE_SIZE);

  if (G_LIKELY(icon_info != NULL)) {
    GdkPixbuf *pix = gtk_icon_info_load_symbolic_for_context(icon_info, style_context, NULL, NULL);

    if (G_LIKELY(pix != NULL)) {
      cairo_surface_t *surface = gdk_cairo_surface_create_from_pixbuf(pix, scale_factor, NULL);

      if (notification_plugin->new_notifications) {
        GIcon *emblem = g_themed_icon_new("org.xfce.notification.unread-emblem-symbolic");
        GtkIconInfo *emblem_info = gtk_icon_theme_lookup_by_gicon_for_scale(icon_theme,
                                                                            emblem,
                                                                            notification_plugin->icon_size,
                                                                            scale_factor,
                                                                            GTK_ICON_LOOKUP_FORCE_SIZE);

        if (G_LIKELY(emblem_info != NULL)) {
          GdkPixbuf *emblem_pix = gtk_icon_info_load_symbolic_for_context(emblem_info, style_context, NULL, NULL);

          if (G_LIKELY(emblem_pix != NULL)) {
            cairo_t *cr = cairo_create(surface);

            cairo_scale(cr, 1.0 / scale_factor, 1.0 / scale_factor);
            gdk_cairo_set_source_pixbuf(cr, emblem_pix, 0, 0);
            cairo_paint_with_alpha(cr, dnd_enabled ? 0.7 : 1.0);

            cairo_destroy(cr);
            g_object_unref(emblem_pix);
          }

          g_object_unref(emblem_info);
        }

        g_object_unref(emblem);
      }

      gtk_image_set_from_surface(GTK_IMAGE(notification_plugin->image), surface);

      cairo_surface_destroy(surface);
      g_object_unref(pix);
    }

    g_object_unref(icon_info);
  }

  g_object_unref(base_icon);

  gtk_widget_set_visible(notification_plugin->button,
                         !notification_plugin->hide_on_read
                         || notification_plugin->new_notifications
                         || gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(notification_plugin->button)));
}



static void
notification_plugin_dnd_updated (XfconfChannel *channel,
                                 const gchar *property,
                                 const GValue *value,
                                 gpointer user_data)
{
  NotificationPlugin *notification_plugin = user_data;
  notification_plugin_update_icon(notification_plugin);
}



static void
cb_notification_log_changed(XfceNotifyLog *log,
                            NotificationPlugin *notification_plugin)
{
  notification_plugin->new_notifications = xfce_notify_log_has_unread_messages(notification_plugin->log);
  notification_plugin_update_icon(notification_plugin);
}



static NotificationPlugin *
notification_plugin_new (XfcePanelPlugin *panel_plugin)
{
  NotificationPlugin    *notification_plugin;
  GError *error = NULL;

  /* Allocate memory for the plugin structure */
  notification_plugin = g_slice_new0 (NotificationPlugin);
  notification_plugin->plugin = panel_plugin;

  notification_plugin->log = xfce_notify_log_open(&error);
  if (notification_plugin->log == NULL) {
    notification_plugin->new_notifications = FALSE;
    g_critical("Unable to open notification log: %s", error->message);
    g_error_free(error);
  } else {
    notification_plugin->new_notifications = xfce_notify_log_has_unread_messages(notification_plugin->log);
    g_signal_connect(notification_plugin->log, "changed",
                     G_CALLBACK(cb_notification_log_changed), notification_plugin);
  }

  /* Initialize xfconf */
  xfconf_init (NULL);
  notification_plugin->channel = xfconf_channel_new ("xfce4-notifyd");

  notification_plugin->hide_on_read = xfconf_channel_get_bool(notification_plugin->channel, SETTING_HIDE_ON_READ, FALSE);
  g_signal_connect(notification_plugin->channel, "property-changed::" SETTING_HIDE_ON_READ,
                   G_CALLBACK(cb_hide_on_read_changed), notification_plugin);


  /* Create the panel widgets (image-button) */
  xfce_panel_plugin_set_small (panel_plugin, TRUE);
  notification_plugin->button = xfce_panel_create_toggle_button ();
  gtk_widget_set_tooltip_text (GTK_WIDGET (notification_plugin->button), _("Notifications"));
  notification_plugin->image = gtk_image_new ();

  gtk_container_add (GTK_CONTAINER (notification_plugin->button), notification_plugin->image);
  gtk_widget_show_all (GTK_WIDGET (notification_plugin->button));
  gtk_widget_set_name (GTK_WIDGET (notification_plugin->button), "xfce4-notification-plugin");

  notification_plugin_size_changed(notification_plugin->plugin,
                                   xfce_panel_plugin_get_size(notification_plugin->plugin),
                                   notification_plugin);

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
  g_signal_connect_swapped(gtk_icon_theme_get_default(), "changed",
                           G_CALLBACK(notification_plugin_update_icon), notification_plugin);

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

  if (notification_plugin->log) {
      g_object_unref(notification_plugin->log);
  }

  g_signal_handlers_disconnect_by_func(gtk_icon_theme_get_default(),
                                       notification_plugin_update_icon,
                                       notification_plugin);

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

  notification_plugin->icon_size = icon_size;
  notification_plugin_update_icon(notification_plugin);

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
