/*
 *  xfce4-notifyd
 *
 *  Copyright (c) 2008 Brian Tarricone <bjt23@cornell.edu>
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

#include <gtk/gtk.h>

#include <xfconf/xfconf.h>
#include <libxfcegui4/libxfcegui4.h>

#include "xfce-notify-daemon.h"

int
main(int argc,
     char **argv)
{
    XfceNotifyDaemon *daemon;
    GError *error = NULL;

    xfconf_init(NULL);
    gtk_init(&argc, &argv);

    daemon = xfce_notify_daemon_new_unique(&error);
    if(!daemon) {
        xfce_message_dialog(NULL, _("Xfce Notify Daemon"),
                            GTK_STOCK_DIALOG_ERROR,
                            _("Unable to start notification daemon"),
                            error->message,
                            GTK_STOCK_QUIT, GTK_RESPONSE_ACCEPT,
                            NULL);
        g_error_free(error);
        return 1;
    }

    gtk_main();

    g_object_unref(G_OBJECT(daemon));

    return 0;
}
