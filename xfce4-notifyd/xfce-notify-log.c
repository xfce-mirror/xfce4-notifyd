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

#include <glib.h>

#include "xfce-notify-log.h"

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
                             const gchar *app_icon,
                             gint expire_timeout,
                             const gchar **actions)
{
    GKeyFile *notify_log;
    gchar *notify_log_path;
    const gchar *timeout;
    const gchar *group;
    gchar **groups;
    gint i;
    gint j = 0;
    GDateTime *now;
    gchar *timestamp;

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
        group = g_strdup_printf ("%s", timestamp);

        g_key_file_set_string (notify_log, group, "app_name", app_name);
        g_key_file_set_string (notify_log, group, "summary", summary);
        g_key_file_set_string (notify_log, group, "body", body);
        g_key_file_set_string (notify_log, group, "app_icon", app_icon);
        timeout = g_strdup_printf ("%d", expire_timeout);
        g_key_file_set_string (notify_log, group, "expire-timeout", timeout);
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
    }
    else
        g_warning ("Unable to open cache file");

    g_free (notify_log_path);
}

void xfce_notify_log_clear (void)
{
    gchar *notify_log = NULL;

    notify_log = xfce_resource_save_location (XFCE_RESOURCE_CACHE,
                                              XFCE_NOTIFY_LOG_FILE, FALSE);

    if (notify_log)
    {
        FILE *f;
        f = fopen (notify_log, "w");
        if (f)
            fclose (f);
        g_free (notify_log);
    }
}
