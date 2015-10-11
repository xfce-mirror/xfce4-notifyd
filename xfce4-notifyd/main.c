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

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <glib/gstdio.h>

#include <gtk/gtk.h>

#include <xfconf/xfconf.h>
#include <libxfce4util/libxfce4util.h>
#include <libxfce4ui/libxfce4ui.h>

#include "xfce-notify-daemon.h"

int
main(int argc,
     char **argv)
{
    XfceNotifyDaemon *xndaemon;
    GError *error = NULL;
    gchar  *temp_theme_file;

    xfconf_init(NULL);

    /* For theming we need to rely on a trick.
     *
     * We can't use gtk_rc_parse to parse theme files because if we do
     * so they get added to the list of rc files for Gtk widgets. Then,
     * the next time you update the theme and parse a new GtkRc file,
     * you still have the old values if the new theme does not override
     * them.
     *
     * Thus, we create a temp file that we add to the list of default
     * GtkRc files. This file will only contain an include to the actual
     * theme file. That way we only have to call gtk_rc_reparse_all to
     * update notifications' style.
     *
     * This has to be done before gtk_init. */
	/*
    temp_theme_file = g_build_path(G_DIR_SEPARATOR_S, g_get_user_cache_dir(),
                                   "xfce4-notifyd-theme.rc", NULL);

    gtk_rc_add_default_file(temp_theme_file);
	*/

    gtk_init(&argc, &argv);

    xfce_textdomain(GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");

    if(argc > 1) {
        if(!strcmp(argv[1], "--version") || !strcmp(argv[1], "-V")) {
            g_print("%s %s\n", _("Xfce Notify Daemon"), VERSION);
            return 0;
        } else {
            g_printerr(_("Unknown option \"%s\"\n"), argv[1]);
            return 1;
        }
    }

    xndaemon = xfce_notify_daemon_new_unique(&error);
    if(!xndaemon) {
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

    /* Remove the temp file for themes */
    g_unlink(temp_theme_file);
    g_free(temp_theme_file);

    g_object_unref(G_OBJECT(xndaemon));

    return 0;
}
