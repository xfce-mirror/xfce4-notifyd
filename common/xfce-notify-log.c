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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxfce4util/libxfce4util.h>

#include <gdk/gdkx.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "xfce-notify-log.h"

GdkPixbuf *
notify_pixbuf_from_image_data (GVariant *image_data)
{
    GdkPixbuf *pix = NULL;
    gint32 width, height, rowstride, bits_per_sample, channels;
    gboolean has_alpha;
    GVariant *pixel_data;
    gsize correct_len;
    guchar *data;

    if (!g_variant_is_of_type (image_data, G_VARIANT_TYPE ("(iiibiiay)")))
    {
        g_warning ("Image data is not the correct type");
        return NULL;
    }

    g_variant_get (image_data,
                   "(iiibii@ay)",
                   &width,
                   &height,
                   &rowstride,
                   &has_alpha,
                   &bits_per_sample,
                   &channels,
                   &pixel_data);

    correct_len = (height - 1) * rowstride + width
                  * ((channels * bits_per_sample + 7) / 8);
    if(correct_len != g_variant_get_size (pixel_data)) {
        g_message ("Pixel data length (%lu) did not match expected value (%u)",
                   g_variant_get_size (pixel_data), (guint)correct_len);
        return NULL;
    }

    data = (guchar *) g_memdup (g_variant_get_data (pixel_data),
                                g_variant_get_size (pixel_data));
    g_variant_unref(pixel_data);

    pix = gdk_pixbuf_new_from_data(data,
                                   GDK_COLORSPACE_RGB, has_alpha,
                                   bits_per_sample, width, height,
                                   rowstride,
                                   (GdkPixbufDestroyNotify)g_free, NULL);
    return pix;
}

const gchar *
notify_icon_name_from_desktop_id (const gchar *desktop_id)
{
    const gchar *icon_file;
    gchar *resource;
    XfceRc *rcfile;

    resource = g_strdup_printf("applications%c%s.desktop",
                               G_DIR_SEPARATOR,
                               desktop_id);
    rcfile = xfce_rc_config_open(XFCE_RESOURCE_DATA,
                                 resource, TRUE);
    g_free (resource);
    if (rcfile && xfce_rc_has_group (rcfile, "Desktop Entry")) {
        xfce_rc_set_group (rcfile, "Desktop Entry");
        icon_file = xfce_rc_read_entry (rcfile, "Icon", NULL);
        xfce_rc_close (rcfile);
        return icon_file;
    }
    else
        return NULL;
}

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

void xfce_notify_log_insert (const gchar *app_name,
                             const gchar *summary,
                             const gchar *body,
                             GVariant *image_data,
                             const gchar *image_path,
                             const gchar *app_icon,
                             const gchar *desktop_id,
                             gint expire_timeout,
                             const gchar **actions)
{
    GKeyFile *notify_log;
    gchar *notify_log_path;
    gchar *timeout;
    gchar *group;
    gchar **groups;
    gint i;
    gint j = 0;
    GDateTime *now;
    gchar *timestamp;
    GBytes *image_bytes;
    gchar *icon_name;
    GdkPixbuf *pixbuf = NULL;
    gchar *notify_log_icon_folder;
    gchar *notify_log_icon_path;

    notify_log_path = xfce_resource_save_location (XFCE_RESOURCE_CACHE,
                                                   XFCE_NOTIFY_LOG_FILE, TRUE);

    if (notify_log_path)
    {
        notify_log = g_key_file_new ();
        if (g_key_file_load_from_file (notify_log, notify_log_path, G_KEY_FILE_NONE, NULL) == FALSE)
        {
            g_warning ("No file found in cache, creating a new log.");
        }

        now = g_date_time_new_now_local ();
        timestamp = g_date_time_format (now, "%FT%T");
        g_date_time_unref (now);
        group = g_strdup_printf ("%s", timestamp);
        g_free(timestamp);

        g_key_file_set_string (notify_log, group, "app_name", app_name);
        g_key_file_set_string (notify_log, group, "summary", summary);
        g_key_file_set_string (notify_log, group, "body", body);
        if (image_data) {
            image_bytes = g_variant_get_data_as_bytes (image_data);
            icon_name = g_compute_checksum_for_bytes (G_CHECKSUM_MD5, image_bytes);
            g_bytes_unref(image_bytes);
            pixbuf = notify_pixbuf_from_image_data (image_data);
            if (pixbuf) {
                notify_log_icon_folder = xfce_resource_save_location (XFCE_RESOURCE_CACHE,
                                                                      XFCE_NOTIFY_ICON_PATH, TRUE);
                notify_log_icon_path = g_strconcat (notify_log_icon_folder , icon_name, ".png", NULL);
                g_free(notify_log_icon_folder);
                if (!g_file_test (notify_log_icon_path, G_FILE_TEST_EXISTS)) {
                    if (!gdk_pixbuf_save (pixbuf, notify_log_icon_path, "png", NULL, NULL))
                        g_warning ("Could not save the pixbuf to: %s", notify_log_icon_path);
                }
                g_free(notify_log_icon_path);
                g_object_unref (G_OBJECT (pixbuf));
            }
            g_key_file_set_string (notify_log, group, "app_icon", icon_name);
            g_free(icon_name);
        }
        else if (image_path) {
            g_key_file_set_string (notify_log, group, "app_icon", image_path);
        }
        else if (app_icon && (g_strcmp0 (app_icon, "") != 0)) {
            g_key_file_set_string (notify_log, group, "app_icon", app_icon);
        }
        else if (desktop_id) {
            g_key_file_set_string (notify_log, group, "app_icon", notify_icon_name_from_desktop_id(desktop_id));
        }

        timeout = g_strdup_printf ("%d", expire_timeout);
        g_key_file_set_string (notify_log, group, "expire-timeout", timeout);
        g_free(timeout);
        for (i = 0; actions && actions[i]; i += 2) {
            const gchar *cur_action_id = actions[i];
            const gchar *cur_button_text = actions[i+1];
            gchar *action_id_num = g_strdup_printf ("%s-%d", "action-id", j);
            gchar *action_label_num = g_strdup_printf ("%s-%d", "action-label", j);
            g_key_file_set_string (notify_log, group, action_id_num, cur_action_id);
            g_key_file_set_string (notify_log, group, action_label_num, cur_button_text);
            j++;
        }

        g_key_file_save_to_file (notify_log, notify_log_path, NULL);
        g_key_file_free (notify_log);
        g_free(group);
    }
    else
        g_warning ("Unable to open cache file");

    g_free (notify_log_path);
}

void xfce_notify_log_clear (void)
{
    gchar *notify_log_path = NULL;

    notify_log_path = xfce_resource_save_location (XFCE_RESOURCE_CACHE,
                                                   XFCE_NOTIFY_LOG_FILE, FALSE);

    if (notify_log_path)
    {
        GFile *log_file;
        log_file = g_file_new_for_path (notify_log_path);
        if (!g_file_delete (log_file, NULL, NULL))
            g_warning ("Could not delete the notification log file: %s", notify_log_path);
        g_object_unref (log_file);
        g_free (notify_log_path);
    }
}
