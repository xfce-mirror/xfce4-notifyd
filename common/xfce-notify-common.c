/*
 *  xfce4-notifyd
 *
 *  Copyright (c) 2023 Brian Tarricone <brian@tarricone.org>
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

#include "xfce-notify-common.h"

// We can't use pango_parse_markup(), as that does not support hyperlinks.
gboolean
xfce_notify_is_markup_valid(const gchar *markup) {
    gboolean valid = FALSE;

    if (markup != NULL) {
        const GMarkupParser parser = { NULL, };
        GMarkupParseContext *ctx;
        gchar *p;
        gboolean needs_root;

        p = (gchar *)markup;
        while (*p != '\0' && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
            ++p;
        }
        needs_root = strncmp(p, "<markup>", 8) == 0;

        ctx = g_markup_parse_context_new(&parser, 0, NULL, NULL);

        valid = (!needs_root || g_markup_parse_context_parse(ctx, "<markup>", -1, NULL))
            && g_markup_parse_context_parse(ctx, markup, -1, NULL)
            && (!needs_root || g_markup_parse_context_parse(ctx, "</markup>", -1, NULL))
            && g_markup_parse_context_end_parse(ctx, NULL);

        g_markup_parse_context_free(ctx);
    }

    return valid;
}
