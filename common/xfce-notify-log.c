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

static void
xfce_notify_log_keyfile_insert1 (GKeyFile *notify_log,
                                 const gchar *app_name,
                                 const gchar *summary,
                                 const gchar *body,
                                 GVariant *image_data,
                                 const gchar *image_path,
                                 const gchar *app_icon,
                                 const gchar *desktop_id,
                                 gint expire_timeout,
                                 const gchar **actions)
{
    gchar *timeout;
    gchar *group;
    gint i;
    gint j = 0;
    GDateTime *now;
    gchar *timestamp;
    gchar *notify_log_icon_folder;

    notify_log_icon_folder = xfce_resource_save_location (XFCE_RESOURCE_CACHE,
                                                         XFCE_NOTIFY_ICON_PATH, TRUE);

    now = g_date_time_new_now_local ();
    timestamp = g_date_time_format_iso8601 (now);
    g_date_time_unref (now);
    group = g_strdup_printf ("%s", timestamp);
    g_free(timestamp);

    g_key_file_set_string (notify_log, group, "app_name", app_name);
    g_key_file_set_string (notify_log, group, "summary", summary);
    g_key_file_set_string (notify_log, group, "body", body);
    if (image_data) {
        GBytes *image_bytes;
        gchar *icon_name;
        GdkPixbuf *pixbuf;

        image_bytes = g_variant_get_data_as_bytes (image_data);
        icon_name = g_compute_checksum_for_bytes (G_CHECKSUM_SHA1, image_bytes);
        g_bytes_unref(image_bytes);
        pixbuf = notify_pixbuf_from_image_data (image_data);
        if (pixbuf) {
            gchar *notify_log_icon_path = g_strconcat (notify_log_icon_folder , icon_name, ".png", NULL);
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
        /* If the image path is in the tmp directory we copy it to the cache directory to make it persistent
           (e.g. Chrome/Chromium uses the tmp directory to store and reference icons, see https://bugzilla.xfce.org/show_bug.cgi?id=15215)*/
        gchar *image_dir = g_path_get_dirname (image_path);
        if (g_strcmp0 ("/tmp", image_dir) == 0) {
            gchar *image_data = NULL;
            gsize image_data_size = 0;
            if (g_file_get_contents (image_path, &image_data, &image_data_size, NULL))
            {
                /* TODO: convert the image to PNG if it isn't a PNG image */
                gchar *image_data_sha1 = g_compute_checksum_for_data (G_CHECKSUM_SHA1, (const guchar*)image_data, image_data_size);
                gchar *filename = g_strconcat (notify_log_icon_folder, image_data_sha1, ".png", NULL);
                if (!g_file_test (filename, G_FILE_TEST_EXISTS))
                {
                    if (g_file_set_contents (filename, image_data, image_data_size, NULL))
                        g_key_file_set_string (notify_log, group, "app_icon", image_data_sha1);
                    else
                        g_warning ("Failed to copy the image from /tmp to the cache directory: %s", filename);
                }
                else
                    g_key_file_set_string (notify_log, group, "app_icon", image_data_sha1);
                g_free (filename);
                g_free (image_data_sha1);
                g_free (image_data);
            }
            else
            {
                g_warning ("Could not read image: %s", image_path);
            }
        }
        else
        {
            g_key_file_set_string (notify_log, group, "app_icon", image_path);
        }
        g_free (image_dir);
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

    g_free (group);
    g_free (notify_log_icon_folder);
}

void xfce_notify_log_insert (const gchar *app_name,
                             const gchar *summary,
                             const gchar *body,
                             GVariant *image_data,
                             const gchar *image_path,
                             const gchar *app_icon,
                             const gchar *desktop_id,
                             gint expire_timeout,
                             const gchar **actions,
                             gint log_max_size)
{
    gchar *notify_log_path;

    notify_log_path = xfce_resource_save_location (XFCE_RESOURCE_CACHE,
                                                   XFCE_NOTIFY_LOG_FILE, TRUE);

    if (notify_log_path)
    {
        GKeyFile *notify_log;
        gchar *data;
        gsize length = 0;

        if (log_max_size > 0)
        {
            GError *error = NULL;

            notify_log = g_key_file_new ();
            if (g_key_file_load_from_file (notify_log, notify_log_path, G_KEY_FILE_NONE, &error) == FALSE)
            {
                DBG ("No file or corrupt file found in cache, creating a new log.");
                if (error != NULL)
                {
                    if (error->code != G_FILE_ERROR_NOENT)
                    {
                        // Try to preserve data successfully parsed from the corrupted file
                        g_key_file_save_to_file (notify_log, notify_log_path, NULL);
                    }
                    g_error_free (error);
                    error = NULL;
                }
                g_key_file_free (notify_log);
                notify_log = NULL;
            }
            else
            {
                gsize num_groups = 0;
                gchar **groups = g_key_file_get_groups (notify_log, &num_groups);
                g_strfreev (groups);
                groups = NULL;
                if (log_max_size > 0 && (gssize)num_groups >= log_max_size)
                {
                    gint i;

                    DBG ("Deleting %d log entries due to maximum number of entries being set to %d.", (gint)num_groups - log_max_size, log_max_size);

                    g_assert (num_groups - log_max_size + 1 <= num_groups);
                    for (i = 0; i < (gssize)num_groups - log_max_size + 1; i++) {
                        g_key_file_remove_group (notify_log, g_key_file_get_start_group (notify_log), &error);
                        if (error)
                        {
                            g_warning ("Failed to delete log entry: %s", error->message);
                            g_error_free (error);
                            error = NULL;
                        }
                    }

                    xfce_notify_log_keyfile_insert1 (notify_log, app_name, summary, body, image_data, image_path, app_icon, desktop_id, expire_timeout, actions);

                    g_key_file_save_to_file (notify_log, notify_log_path, NULL);
                    goto cleanup;
                }
                else
                {
                    // Use the more efficient g_file_append_to() below
                    g_key_file_free (notify_log);
                    notify_log = NULL;
                }
            }
            g_assert (notify_log == NULL);
        }

        notify_log = g_key_file_new ();
        xfce_notify_log_keyfile_insert1 (notify_log, app_name, summary, body, image_data, image_path, app_icon, desktop_id, expire_timeout, actions);
        data = g_key_file_to_data (notify_log, &length, NULL);
        if (data)
        {
            GFile *notify_log_file = g_file_new_for_path (notify_log_path);
            GFileOutputStream *stream = g_file_append_to (notify_log_file, G_FILE_CREATE_NONE, NULL, NULL);
            if (stream)
            {
                g_output_stream_write_all (G_OUTPUT_STREAM(stream), "\n", strlen("\n"), NULL, NULL, NULL);
                if (g_output_stream_write_all (G_OUTPUT_STREAM(stream), data, length, NULL, NULL, NULL) == FALSE)
                {
                    g_warning ("Failed to append a new entry to notify log file");
                }
                if (g_output_stream_close (G_OUTPUT_STREAM(stream), NULL, NULL) == FALSE)
                {
                    g_warning ("Failed to close notify log file");
                }
                g_object_unref (stream);
            }
            else
            {
                g_warning ("Failed to open notify log file in append mode");
            }
            g_object_unref (notify_log_file);
            g_free (data);
        }
        else
        {
            g_warning ("Failed to serialize a log entry");
        }

    cleanup:
        g_key_file_free (notify_log);
    }
    else
        g_warning ("Unable to open cache file");

    g_free (notify_log_path);
}

gchar *
xfce_notify_get_icon_cache_size (void)
{
    gchar *notify_icon_cache_path = NULL;
    gchar *size_string;

    notify_icon_cache_path = xfce_resource_save_location (XFCE_RESOURCE_CACHE,
                                                          XFCE_NOTIFY_ICON_PATH, FALSE);
    if (notify_icon_cache_path)
    {
        GFile *icon_folder;
        guint64 disk_usage, num_dirs, num_files;

        icon_folder = g_file_new_for_path (notify_icon_cache_path);

        g_file_measure_disk_usage (icon_folder,
                                   G_FILE_MEASURE_NONE,
                                   NULL, NULL, NULL,
                                   &disk_usage,
                                   &num_dirs,
                                   &num_files,
                                   NULL);
        size_string = g_strdup_printf ("%d icons / %.1lf MB",
                                       (int) num_files, (gdouble) disk_usage / 1000000);
        g_object_unref (icon_folder);
    }
    g_free (notify_icon_cache_path);
    return size_string;
}

void xfce_notify_clear_icon_cache (void)
{
    gchar *notify_icon_cache_path = NULL;

    notify_icon_cache_path = xfce_resource_save_location (XFCE_RESOURCE_CACHE,
                                                          XFCE_NOTIFY_ICON_PATH, FALSE);

    if (notify_icon_cache_path)
    {
        GFile *icon_folder;
        GFileEnumerator *folder_contents;


        icon_folder = g_file_new_for_path (notify_icon_cache_path);
        folder_contents = g_file_enumerate_children (icon_folder,
                                                     G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                     G_FILE_QUERY_INFO_NONE,
                                                     NULL,
                                                     NULL);
        /* Iterate over the folder and delete each file */
        while (TRUE)
        {
            GFile *icon_file;
            if (!g_file_enumerator_iterate (folder_contents, NULL, &icon_file, NULL, NULL))
                goto out;
            if (!icon_file)
                break;
            if (!g_file_delete (icon_file, NULL, NULL))
                g_warning ("Could not delete a notification icon: %s", notify_icon_cache_path);
        }
        out:
            g_object_unref (folder_contents);

        /* Delete the empty folder */
        if (!g_file_delete (icon_folder, NULL, NULL))
            g_warning ("Could not delete the notification icon cache: %s", notify_icon_cache_path);

        g_object_unref (icon_folder);
        g_free (notify_icon_cache_path);
    }
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

static void
xfce_notify_clear_log_dialog_cb (GtkWidget *dialog, gint response, gpointer user_data)
{
    GtkWidget *checkbutton = user_data;
    gboolean active;

    active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbutton));

    if (response == GTK_RESPONSE_DELETE_EVENT ||
        response == GTK_RESPONSE_CANCEL)
        return;
    else if (active) {
        xfce_notify_clear_icon_cache ();
        xfce_notify_log_clear ();
    }
    else {
        xfce_notify_log_clear ();
    }
}

GtkWidget *xfce_notify_clear_log_dialog (void)
{
    GtkWidget *dialog, *grid, *icon, *label, *content_area, *checkbutton;
    GtkDialogFlags flags = GTK_DIALOG_MODAL;
    gchar *message;
    const char *str = _("Do you really want to clear the notification log?");
    const char *format = "<span weight='bold' size='large'>%s</span>";
    char *markup;

    dialog = gtk_dialog_new_with_buttons (_("Clear notification log"),
                                          NULL,
                                          flags,
                                          _("Cancel"),
                                          GTK_RESPONSE_CANCEL,
                                          _("Clear"),
                                          GTK_RESPONSE_OK,
                                          NULL);
    content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    grid = gtk_grid_new ();
    gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
    gtk_widget_set_margin_start (grid, 12);
    gtk_widget_set_margin_end (grid, 12);
    gtk_widget_set_margin_top (grid, 12);
    gtk_widget_set_margin_bottom (grid, 12);

    icon = gtk_image_new_from_icon_name ("edit-clear", GTK_ICON_SIZE_DIALOG);

    message = g_strdup_printf ("%s (%s)",_("include icon cache"), xfce_notify_get_icon_cache_size ());
    label = gtk_label_new (NULL);
    markup = g_markup_printf_escaped (format, str);
    gtk_label_set_markup (GTK_LABEL (label), markup);
    g_free (markup);

    checkbutton = gtk_check_button_new_with_label (message);

    gtk_grid_attach (GTK_GRID (grid), icon, 0, 0, 1, 2);
    gtk_grid_attach (GTK_GRID (grid), label, 1, 0, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), checkbutton, 1, 1, 1, 1);

    gtk_container_add (GTK_CONTAINER (content_area), grid);
    gtk_widget_show_all (dialog);

    g_signal_connect (dialog, "response",
                      G_CALLBACK (xfce_notify_clear_log_dialog_cb),
                      checkbutton);
    gtk_window_set_icon_name (GTK_WINDOW (dialog), "edit-clear");

    return dialog;
}
