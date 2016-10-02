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

 /* The following tests will try to determine if the notification server acts in
    conformance with the following wording of the freedesktop.org specification:
    "An implementation which only displays one image or icon must choose which
     one to display using the following order:"
      "image-data"
      "image-path"
      app_icon parameter
      for compatibility reason, "icon_data" */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <stdlib.h>
#include <libnotify/notify.h>
#include <gtk/gtk.h>

static gboolean
show_notification (NotifyNotification *notification) {
    if (!notify_notification_show (notification, NULL))
    {
        g_error ("Failed");
        g_object_unref (notification);

        return EXIT_FAILURE;
    }
    g_object_unref (notification);
    return EXIT_SUCCESS;
}

int main (int argc, char **argv)
{
    NotifyNotification *notification;
    GdkPixbuf *image_data;

    if (!notify_init ("Notification with icon tests"))
    {
        g_error ("Failed to initialize libnotify.");

        return EXIT_FAILURE;
    }

    gtk_init(&argc, &argv);

    image_data = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                           "xfce4-notifyd", 48,
                                           GTK_ICON_LOOKUP_GENERIC_FALLBACK, NULL);

    g_print ("%s", "Testing notification with app_icon parameter\n");
    notification = notify_notification_new ("Test app_icon support",
                                            "Does it work?",
                                            "xfce4-notifyd");
    show_notification (notification);

    g_print ("%s", "Testing notification with the image-path hint\n");
    notification = notify_notification_new ("Test 'image-path' hint support",
                                            "Does it work?",
                                            NULL);
    notify_notification_set_hint_string (notification,
                                         "image-path",
                                         "xfce4-notifyd");
    show_notification (notification);

    g_print ("%s", "Testing notification with the image-data hint\n");
    notification = notify_notification_new ("Test 'image-data' and 'image_data' hint support",
                                            "Does it work?",
                                            NULL);
    notify_notification_set_image_from_pixbuf (notification, image_data);
    show_notification (notification);

    /* The priority tests are stll dummies. Need to decide whether to ship our own
       icons for testing or whether to use standard named icons and hope that they're
       installed or available in the currently selected theme and its fallbacks. */
    g_print ("%s", "Testing priorities with app_icon versus image-path\n");
    notification = notify_notification_new ("Test priorities: app_icon vs. image-path",
                                            "image-path should be shown.",
                                            "xfce4-notifyd");
    notify_notification_set_hint_string (notification,
                                         "image-path",
                                         "xfce4-notifyd");
    show_notification (notification);

    g_print ("%s", "Testing priorities with app_icon versus image-data\n");
    notification = notify_notification_new ("Test priorities: app_icon vs. image-data",
                                            "image-data should be shown.",
                                            "xfce4-notifyd");
    notify_notification_set_image_from_pixbuf (notification, image_data);
    show_notification (notification);

    g_print ("%s", "Testing priorities with image-path versus image-data\n");
    notification = notify_notification_new ("Test priorities: image-path vs. image-data",
                                            "image-data should be shown.",
                                            NULL);
    notify_notification_set_hint_string (notification,
                                         "image-path",
                                         "xfce4-notifyd");
    notify_notification_set_image_from_pixbuf (notification, image_data);
    show_notification (notification);

    g_print ("%s", "Testing priorities with app_icon vs. image-path vs. image-data\n");
    notification = notify_notification_new ("Test priorities: app_icon vs. image-path vs. image-data",
                                            "image-data should be shown.",
                                            "xfce4-notifyd");
    notify_notification_set_hint_string (notification,
                                         "image-path",
                                         "xfce4-notifyd");
    notify_notification_set_image_from_pixbuf (notification, image_data);
    show_notification (notification);

    g_print ("%s", "Testing support for symbolic icons (partly depends on currently selected theme)\n");
    notification = notify_notification_new ("Test support for symbolic icons",
                                            "Is it correctly colored?",
                                            "xfce4-notifyd-symbolic");
    show_notification (notification);

    g_object_unref (image_data);

    return EXIT_SUCCESS;
}
