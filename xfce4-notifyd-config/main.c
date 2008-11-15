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

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <gtk/gtk.h>
#include <glade/glade.h>

#include <dbus/dbus-glib.h>

#include <xfconf/xfconf.h>
#include <libxfcegui4/libxfcegui4.h>

#include "xfce4-notifyd-config.glade.h"

/* unfortunately, currently we have to kill the daemon to
 * change themes.  this is only annoying because existing notifications
 * will get killed */
static void
xfce4_notifyd_config_kill_daemon()
{
    DBusGConnection *dbus_conn;
    DBusGProxy *proxy;

    dbus_conn = dbus_g_bus_get(DBUS_BUS_SESSION, NULL);
    if(!dbus_conn)
        return;

    proxy = dbus_g_proxy_new_for_name(dbus_conn,
                                      "org.freedesktop.Notifications",
                                      "/org/freedesktop/Notifications",
                                      "org.xfce.Notifyd");
    dbus_g_proxy_call_no_reply(proxy, "Quit", G_TYPE_INVALID);

    g_object_unref(G_OBJECT(proxy));
    dbus_g_connection_unref(dbus_conn);
}

static gchar *
xfce4_notifyd_slider_format_value(GtkScale *slider,
                                  gdouble value,
                                  gpointer user_data)
{
    return g_strdup_printf("%d%%", (gint)(value * 100));
}

static void
xfce4_notifyd_config_theme_treeview_changed(GtkTreeSelection *sel,
                                            gpointer user_data)
{
    XfconfChannel *channel = user_data;
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;
    gchar *theme = NULL;

    if(!gtk_tree_selection_get_selected(sel, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, 0, &theme, -1);
    xfconf_channel_set_string(channel, "/theme", theme);
    g_free(theme);
}

static void
xfce4_notifyd_config_theme_changed(XfconfChannel *channel,
                                   const gchar *property,
                                   const GValue *value,
                                   gpointer user_data)
{
    GtkWidget *treeview = user_data;
    GtkListStore *ls;
    GtkTreeSelection *sel;
    GtkTreeIter iter;
    gchar *theme;
    const gchar *new_theme;

    new_theme = G_VALUE_TYPE(value) ? g_value_get_string(value) : "Default";

    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    ls = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(treeview)));

    for(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(ls), &iter);
        gtk_list_store_iter_is_valid(ls, &iter);
        gtk_tree_model_iter_next(GTK_TREE_MODEL(ls), &iter))
    {
        gtk_tree_model_get(GTK_TREE_MODEL(ls), &iter, 0, &theme, -1);
        if(!strcmp(theme, new_theme)) {
            gtk_tree_selection_select_iter(sel, &iter);
            g_free(theme);
            xfce4_notifyd_config_kill_daemon();
            return;
        }
        g_free(theme);
    }

    gtk_list_store_append(ls, &iter);
    gtk_list_store_set(ls, &iter, 0, new_theme, -1);
    gtk_tree_selection_select_iter(sel, &iter);
}

static void
list_store_add_themes_in_dir(GtkListStore *ls,
                             const gchar *path,
                             const gchar *current_theme,
                             GHashTable *themes_seen,
                             GtkTreeIter *current_theme_iter)
{
    GDir *dir;
    const gchar *file;
    gchar buf[PATH_MAX];

    dir = g_dir_open(path, 0, NULL);
    if(!dir)
        return;

    while((file = g_dir_read_name(dir))) {
        if(g_hash_table_lookup(themes_seen, file))
            continue;

        g_snprintf(buf, sizeof(buf), "%s/%s/xfce-notify-4.0/gtkrc",
                   path, file);
        if(g_file_test(buf, G_FILE_TEST_IS_REGULAR)) {
            GtkTreeIter iter;

            gtk_list_store_append(ls, &iter);
            gtk_list_store_set(ls, &iter, 0, file, -1);
            g_hash_table_insert(themes_seen, g_strdup(file),
                                GUINT_TO_POINTER(1));

            if(!strcmp(file, current_theme))
                memcpy(current_theme_iter, &iter, sizeof(iter));
        }
    }

    g_dir_close(dir);
}

static void
xfce4_notifyd_config_setup_treeview(GtkWidget *treeview,
                                    const gchar *current_theme)
{
    GtkListStore *ls;
    gchar *dirname, **dirnames;
    GHashTable *themes_seen;
    gint i;
    GtkCellRenderer *render;
    GtkTreeViewColumn *col;
    GtkTreeIter current_theme_iter;
    GtkTreeSelection *sel;

    ls = gtk_list_store_new(1, G_TYPE_STRING);
    themes_seen = g_hash_table_new_full(g_str_hash, g_str_equal,
                                        (GDestroyNotify)g_free, NULL);

    dirname = g_build_filename(xfce_get_homedir(), ".themes", NULL);
    list_store_add_themes_in_dir(ls, dirname, current_theme,
                                 themes_seen, &current_theme_iter);
    g_free(dirname);

    dirnames = xfce_resource_lookup_all(XFCE_RESOURCE_DATA, "themes/");
    for(i = 0; dirnames && dirnames[i]; ++i)
        list_store_add_themes_in_dir(ls, dirnames[i], current_theme,
                                     themes_seen, &current_theme_iter);
    g_strfreev(dirnames);

    g_hash_table_destroy(themes_seen);

    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(ls));

    render = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(_("Theme"), render,
                                                   "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);

    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(ls), 0,
                                         GTK_SORT_ASCENDING);
    
    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    if(gtk_list_store_iter_is_valid(ls, &current_theme_iter))
        gtk_tree_selection_select_iter(sel, &current_theme_iter);

    g_object_unref(G_OBJECT(ls));
}

static GtkWidget *
xfce4_notifyd_config_setup_dialog(GladeXML *gxml)
{
    XfconfChannel *channel;
    GtkWidget *dlg, *btn, *sbtn, *slider, *chk, *treeview, *combo;
    GtkAdjustment *adj;
    GtkTreeSelection *sel;
    GError *error = NULL;
    gchar *current_theme;

    glade_xml_signal_autoconnect(gxml);

    dlg = glade_xml_get_widget(gxml, "notifyd_settings_dlg");

    btn = glade_xml_get_widget(gxml, "close_btn");
    g_signal_connect_swapped(G_OBJECT(btn), "clicked",
                             G_CALLBACK(gtk_dialog_response), dlg);

    if(!xfconf_init(&error)) {
        xfce_message_dialog(NULL, _("Xfce Notify Daemon"),
                            GTK_STOCK_DIALOG_ERROR,
                            _("Settings daemon is unavailable"),
                            error->message,
                            GTK_STOCK_QUIT, GTK_RESPONSE_ACCEPT,
                            NULL);
        exit(EXIT_FAILURE);
    }

    channel = xfconf_channel_new("xfce4-notifyd");

    sbtn = glade_xml_get_widget(gxml, "expire_timeout_sbtn");
    xfconf_g_property_bind(channel, "/expire-timeout", G_TYPE_INT,
                           G_OBJECT(sbtn), "value");

    slider = glade_xml_get_widget(gxml, "opacity_slider");
    g_signal_connect(G_OBJECT(slider), "format-value",
                     G_CALLBACK(xfce4_notifyd_slider_format_value), NULL);
    adj = gtk_range_get_adjustment(GTK_RANGE(slider));
    xfconf_g_property_bind(channel, "/initial-opacity", G_TYPE_DOUBLE,
                           G_OBJECT(adj), "value");

    chk = glade_xml_get_widget(gxml, "fade_transparency_chk");
    xfconf_g_property_bind(channel, "/fade-transparency", G_TYPE_BOOLEAN,
                           G_OBJECT(chk), "active");

    treeview = glade_xml_get_widget(gxml, "themes_treeview");
    current_theme = xfconf_channel_get_string(channel, "/theme", "Default");
    xfce4_notifyd_config_setup_treeview(treeview, current_theme);
    g_free(current_theme);

    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    g_signal_connect(G_OBJECT(sel), "changed",
                     G_CALLBACK(xfce4_notifyd_config_theme_treeview_changed),
                     channel);
    g_signal_connect(G_OBJECT(channel), "property-changed::/theme",
                     G_CALLBACK(xfce4_notifyd_config_theme_changed),
                     treeview);

    combo = glade_xml_get_widget(gxml, "position_combo");
    xfconf_g_property_bind(channel, "/notify-location", G_TYPE_UINT,
                           G_OBJECT(combo), "active");
    if(gtk_combo_box_get_active(GTK_COMBO_BOX(combo)) == -1)
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), GTK_CORNER_TOP_RIGHT);
    
    return dlg;
}

int
main(int argc,
     char **argv)
{
    GtkWidget *settings_dialog = NULL;
    GladeXML *gxml;
    gboolean opt_version = FALSE;
    GdkNativeWindow opt_socket_id = 0;
    GOptionEntry option_entries[] = {
        { "version", 'V', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &opt_version, N_("Display version information"), NULL },
        { "socket-id", 's', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT, &opt_socket_id, N_("Settings manager socket"), N_("SOCKET_ID") },
        { NULL, },
    };
    GError *error = NULL;

    xfce_textdomain(GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");

    if(!gtk_init_with_args(&argc, &argv, "", option_entries, PACKAGE, &error)) {
        if(G_LIKELY(error)) {
            g_printerr("%s: %s.\n", G_LOG_DOMAIN, error->message);
            g_printerr(_("Type '%s --help' for usage."), G_LOG_DOMAIN);
            g_printerr("\n");
            g_error_free(error);
        } else
            g_error("Unable to open display.");

        return EXIT_FAILURE;
    }

    if(G_UNLIKELY(opt_version)) {
        g_print("%s %s\n", G_LOG_DOMAIN, VERSION);
        g_print(_("Copyright (c) 2008 Brian Tarricone <bjt23@cornell.edu>\n"));
        g_print(_("Released under the terms of the GNU General Public License, version 2\n"));
        g_print(_("Please report bugs to %s.\n"), PACKAGE_BUGREPORT);

        return EXIT_SUCCESS;
    }

    gxml = glade_xml_new_from_buffer(xfce4_notifyd_config_glade,
                                     xfce4_notifyd_config_glade_length,
                                     NULL, NULL);
    if(!gxml) {
        xfce_message_dialog(NULL, _("Xfce Notify Daemon"),
                            GTK_STOCK_DIALOG_ERROR,
                            _("Unable to display settings dialog"),
                            _("The embedded user interface definition file could not be read"),
                            GTK_STOCK_QUIT, GTK_RESPONSE_ACCEPT,
                            NULL);
        return EXIT_FAILURE;
    }

    settings_dialog = xfce4_notifyd_config_setup_dialog(gxml);

    if(opt_socket_id) {
        GtkWidget *plug, *plug_child;

        plug = gtk_plug_new(opt_socket_id);
        gtk_widget_show(plug);
        g_signal_connect(G_OBJECT(plug), "delete-event",
                         G_CALLBACK(gtk_main_quit), NULL);

        plug_child = glade_xml_get_widget(gxml, "plug-child");
        gtk_widget_reparent(plug_child, plug);
        gtk_widget_show(plug_child);

        gdk_notify_startup_complete();
        g_object_unref(G_OBJECT(gxml));
        gtk_widget_destroy(settings_dialog);

        gtk_main();
    } else {
        g_object_unref(G_OBJECT(gxml));

        gtk_dialog_run(GTK_DIALOG(settings_dialog));
    }

    return EXIT_SUCCESS;
}
