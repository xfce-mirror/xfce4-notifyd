/* vi:set et ai sw=4 sts=4 ts=4: */
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

#include "xfce-notify-types.h"

GType
xfce_notify_urgency_get_type(void) {
    static GType type = 0;

    if (type == 0) {
        static const GEnumValue values[] = {
            { XFCE_NOTIFY_URGENCY_LOW, "XFCE_NOTIFY_URGENCY_LOW", "low" },
            { XFCE_NOTIFY_URGENCY_NORMAL, "XFCE_NOTIFY_URGENCY_NORMAL", "normal" },
            { XFCE_NOTIFY_URGENCY_CRITICAL, "XFCE_NOTIFY_URGENCY_CRITICAL", "critical" },
            { 0, NULL, NULL },
        };
        type = g_enum_register_static("XfceNotifyUrgency", values);
    }

    return type;
}

GType
xfce_notify_close_reason_get_type(void)
{
	static GType type = 0;

    if (type == 0) {
        static const GEnumValue values[] = {
            { XFCE_NOTIFY_CLOSE_REASON_EXPIRED, "XFCE_NOTIFY_CLOSE_REASON_EXPIRED", "expired" },
            { XFCE_NOTIFY_CLOSE_REASON_DISMISSED, "XFCE_NOTIFY_CLOSE_REASON_DISMISSED", "dismissed" },
            { XFCE_NOTIFY_CLOSE_REASON_CLIENT, "XFCE_NOTIFY_CLOSE_REASON_CLIENT", "client" },
            { XFCE_NOTIFY_CLOSE_REASON_UNKNOWN, "XFCE_NOTIFY_CLOSE_REASON_UNKNOWN", "unknown" },
            { 0, NULL, NULL },
        };
        type = g_enum_register_static("XfceNotifyCloseReason", values);
    }

    return type;
}

void
xfce_notification_actions_free(XfceNotificationActions *actions) {
    if (actions != NULL) {
        for (guint i = 0; i < actions->n_actions; ++i) {
            g_free(actions->actions[i].id);
            g_free(actions->actions[i].label);
        }
        g_free(actions->actions);
        g_free(actions);
    }
}
