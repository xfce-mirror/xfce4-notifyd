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

gchar **
xfce_notify_log_get (void)
{
    gchar **lines = NULL;
    gchar *notify_log = NULL;
    gchar *contents = NULL;
    gsize length = 0;

    notify_log = xfce_resource_lookup (XFCE_RESOURCE_CACHE, XFCE_NOTIFY_LOG_FILE);

    if (notify_log && g_file_get_contents (notify_log, &contents, &length, NULL))
    {
        lines = g_strsplit (contents, "\n", -1);
        g_free (contents);
    }

    g_free (notify_log);

    return lines;
}

void xfce_notify_log_insert_line (const gchar *line)
{
    gchar *notify_log = NULL;

    notify_log = xfce_resource_save_location (XFCE_RESOURCE_CACHE,
                                              XFCE_NOTIFY_LOG_FILE, TRUE);

    if (notify_log)
    {
        FILE *f;
        f = fopen (notify_log, "a");
        fprintf (f, "%s\n", line);
        fclose (f);
        g_free (notify_log);
    }
    else
        g_warning ("Unable to open cache file");
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
        fclose (f);
        g_free (notify_log);
    }
}
