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

#include <xfconf/xfconf.h>
#include <libxfcegui4/libxfcegui4.h>

#include "xfce4-notifyd-config.glade.h"

static gchar *
xfce4_notifyd_slider_format_value(GtkScale *slider,
                                  gdouble value,
                                  gpointer user_data)
{
    return g_strdup_printf("%d%%", (gint)(value * 100));
}

static GtkWidget *
xfce4_notifyd_config_setup_dialog(GladeXML *gxml)
{
    XfconfChannel *channel;
    GtkWidget *dlg, *btn, *sbtn, *slider, *chk;
    GtkAdjustment *adj;
    GError *error = NULL;

    glade_xml_signal_autoconnect(gxml);

    dlg = glade_xml_get_widget(gxml, "notifyd_settings_dlg");
    g_signal_connect(G_OBJECT(dlg), "response",
                     G_CALLBACK(gtk_main_quit), NULL);

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
        exit(1);
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

    return dlg;
}

int
main(int argc,
     char **argv)
{
    GtkWidget *settings_dialog = NULL;
    GladeXML *gxml;

    gtk_init(&argc, &argv);

    xfce_textdomain(GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");

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
        return 1;
    }

    settings_dialog = xfce4_notifyd_config_setup_dialog(gxml);
    gtk_widget_show(settings_dialog);
    g_object_add_weak_pointer(G_OBJECT(settings_dialog),
                              (gpointer)&settings_dialog);

    gtk_main();

    if(settings_dialog)
        gtk_widget_destroy(settings_dialog);

    return 0;
}
