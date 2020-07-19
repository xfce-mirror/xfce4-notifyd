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
#include <gtk/gtkx.h>

#include <xfconf/xfconf.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libnotify/notify.h>

#include <common/xfce-notify-log.h>

#include "xfce4-notifyd-config.ui.h"

#define KNOWN_APPLICATIONS_PROP       "/applications/known_applications"
#define MUTED_APPLICATIONS_PROP       "/applications/muted_applications"
#define LOG_DISPLAY_LIMIT             100

typedef struct
{
    GtkWidget  *log_level;
    GtkWidget  *log_level_apps;
    GtkWidget  *log_level_apps_label;
    GtkWidget  *infobar_label;
    GtkWidget  *log_listbox;
    GtkToolbar *log_toolbar;
} NotificationLogWidgets;

typedef struct
{
    GtkWidget  *do_slideout;
    GtkWidget  *do_slideout_label;
} NotificationSlideoutWidgets;

static void
xfce_notifyd_config_show_notification_callback(NotifyNotification *notification,
                                               const char         *action,
                                               gpointer            unused)
{
  /* Don't do anything, we just have a button to show its style */
}

static void
xfce_notifyd_config_show_notification_preview(GtkWindow *parent_window)
{
    NotifyNotification *notification;
    GError             *error = NULL;

    notification =
        notify_notification_new(_("Notification Preview"),
                                _("This is what notifications will look like"),
                                "xfce4-notifyd");

    notify_notification_add_action(notification,
                                   "button",
                                   _("Button"),
                                   (NotifyActionCallback) xfce_notifyd_config_show_notification_callback,
                                   NULL,
                                   NULL);

    if (!notify_notification_show(notification, &error)) {
        xfce_dialog_show_error(parent_window, error,
                               _("Notification preview failed"));

        g_error_free(error);
    }

    g_object_unref(notification);
}

static gchar *
xfce4_notifyd_slider_format_value(GtkScale *slider,
                                  gdouble value,
                                  gpointer user_data)
{
    return g_strdup_printf("%d%%", (gint)(value * 100));
}

static void
xfce4_notifyd_config_theme_combo_changed(GtkComboBox *theme_combo,
                                         gpointer user_data)
{
    XfconfChannel *channel = user_data;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *theme = NULL;

    if(!gtk_combo_box_get_active_iter(theme_combo, &iter))
        return;

    model = gtk_combo_box_get_model (theme_combo);

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
    GtkWidget *theme_combo = user_data;
    GtkListStore *ls;
    GtkTreeIter iter;
    gchar *theme;
    const gchar *new_theme;

    new_theme = G_VALUE_TYPE(value) ? g_value_get_string(value) : "Default";

    ls = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(theme_combo)));

    for(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(ls), &iter);
        gtk_list_store_iter_is_valid(ls, &iter);
        gtk_tree_model_iter_next(GTK_TREE_MODEL(ls), &iter))
    {
        gtk_tree_model_get(GTK_TREE_MODEL(ls), &iter, 0, &theme, -1);
        if(!strcmp(theme, new_theme)) {

            gtk_combo_box_set_active_iter(GTK_COMBO_BOX(theme_combo),
                                          &iter);
            g_free(theme);

            xfce_notifyd_config_show_notification_preview(GTK_WINDOW(gtk_widget_get_toplevel(theme_combo)));

            return;
        }
        g_free(theme);
    }

    gtk_list_store_append(ls, &iter);
    gtk_list_store_set(ls, &iter, 0, new_theme, -1);
    gtk_combo_box_set_active_iter(GTK_COMBO_BOX(theme_combo), &iter);
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
    gchar *filename;
    gchar *old_filename = NULL;

    dir = g_dir_open(path, 0, NULL);
    if(!dir)
        return;

    while((file = g_dir_read_name(dir))) {
        if(g_hash_table_lookup(themes_seen, file))
            continue;

        filename =
            g_build_filename(path, file, "xfce-notify-4.0", "gtk.css", NULL);

        if (g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
            GtkTreeIter iter;
            if (old_filename != NULL) {
                if (g_ascii_strcasecmp (old_filename, filename) < 0)
                    gtk_list_store_append(ls, &iter);
                else
                    gtk_list_store_prepend(ls, &iter);
                g_free (old_filename);
            }
            else
                gtk_list_store_append(ls, &iter);

            gtk_list_store_set(ls, &iter, 0, file, -1);
            g_hash_table_insert(themes_seen, g_strdup(file),
                                GUINT_TO_POINTER(1));

            if(!strcmp(file, current_theme))
                memcpy(current_theme_iter, &iter, sizeof(iter));
        }

        old_filename = g_strdup (filename);
        g_free(filename);
    }
    g_free (old_filename);

    g_dir_close(dir);
}

static void
xfce4_notifyd_config_setup_theme_combo(GtkWidget *theme_combo,
                                       const gchar *current_theme)
{
    GtkListStore *ls;
    gchar *dirname, **dirnames;
    GHashTable *themes_seen;
    gint i;
    GtkTreeIter current_theme_iter;

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

    gtk_combo_box_set_model(GTK_COMBO_BOX(theme_combo), GTK_TREE_MODEL(ls));

    if(gtk_list_store_iter_is_valid(ls, &current_theme_iter))
        gtk_combo_box_set_active_iter(GTK_COMBO_BOX(theme_combo),
                                      &current_theme_iter);

    g_object_unref(G_OBJECT(ls));
}

static void
xfce_notifyd_config_dialog_response(GtkWidget *dialog, gint response, gpointer unused)
{
  if (response == 0)
      g_signal_stop_emission_by_name (dialog, "response");
}

static void
xfce_notifyd_config_preview_clicked(GtkButton *button)
{
  GtkWidget *window = gtk_widget_get_toplevel (GTK_WIDGET (button));

  xfce_notifyd_config_show_notification_preview (GTK_WINDOW (window));
}

/* Shows a separator before each row. */
static void
display_header_func (GtkListBoxRow *row,
                     GtkListBoxRow *before,
                     gpointer       user_data)
{
  if (before != NULL)
    {
      GtkWidget *header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
      gtk_widget_show (header);

      gtk_list_box_row_set_header (row, header);
    }
}

static void
xfce4_notifyd_mute_application (GtkListBox *known_applications_listbox,
                                GtkListBoxRow *selected_application_row,
                                XfconfChannel *channel)
{

    GtkWidget *application_box;
    GtkWidget *application_label;
    GtkWidget *mute_switch;
    gboolean muted;
    GPtrArray *muted_applications;
    GValue *val;
    guint i;
    const gchar *application_name;
    gchar *new_app_name;

    muted_applications = xfconf_channel_get_arrayv (channel, MUTED_APPLICATIONS_PROP);
    if (muted_applications == NULL)
        muted_applications = g_ptr_array_new ();

    application_box = GTK_WIDGET (g_list_nth_data (gtk_container_get_children (GTK_CONTAINER (selected_application_row)), 0));
    application_label = GTK_WIDGET (g_list_nth_data (gtk_container_get_children (GTK_CONTAINER (application_box)), 1));
    application_name = gtk_label_get_text (GTK_LABEL(application_label));
    mute_switch = GTK_WIDGET (g_list_nth_data (gtk_container_get_children (GTK_CONTAINER (application_box)), 2));
    muted = gtk_switch_get_active (GTK_SWITCH (mute_switch));

    val = g_new0 (GValue, 1);
    g_value_init (val, G_TYPE_STRING);
    new_app_name = g_strdup (application_name);
    g_value_take_string (val, new_app_name);
    if (muted == TRUE && muted_applications != NULL) {
        for (i = 0; i < muted_applications->len; i++) {
            GValue *muted_application;
            muted_application = g_ptr_array_index (muted_applications, i);
            if (g_str_match_string (g_value_get_string (val), g_value_get_string (muted_application), FALSE) == TRUE) {
                if (!g_ptr_array_remove_index (muted_applications, i))
                    g_warning ("Could not remove %s from the list of muted applications. %i", new_app_name, i);
                break;
            }

        }

    }
    else
        g_ptr_array_add (muted_applications, val);

    if (!xfconf_channel_set_arrayv (channel, MUTED_APPLICATIONS_PROP, muted_applications))
        g_warning ("Could not add %s to the list of muted applications.", new_app_name);

    xfconf_array_free (muted_applications);
}

static void
xfce4_notifyd_row_activated (GtkListBox *known_applications_listbox,
                             GtkListBoxRow *selected_application_row,
                             gpointer user_data)
{
    GtkWidget *application_box;
    GtkWidget *mute_switch;
    gboolean muted;

    application_box = GTK_WIDGET (g_list_nth_data (gtk_container_get_children (GTK_CONTAINER (selected_application_row)), 0));
    mute_switch = GTK_WIDGET (g_list_nth_data (gtk_container_get_children (GTK_CONTAINER (application_box)), 2));
    muted = !gtk_switch_get_active (GTK_SWITCH (mute_switch));
    gtk_switch_set_active (GTK_SWITCH (mute_switch), muted);
}

static void
xfce4_notifyd_switch_activated (GtkSwitch *mute_switch,
                                gboolean state,
                                gpointer user_data)
{
    XfconfChannel *channel = user_data;
    GtkWidget *row_box;
    GtkWidget *selected_application_row;
    GtkWidget *known_applications_listbox;

    g_return_if_fail (XFCONF_IS_CHANNEL (channel));

    row_box = gtk_widget_get_parent (GTK_WIDGET (mute_switch));
    selected_application_row = gtk_widget_get_parent (GTK_WIDGET (row_box));
    known_applications_listbox = gtk_widget_get_parent (GTK_WIDGET (selected_application_row));
    xfce4_notifyd_mute_application (GTK_LIST_BOX (known_applications_listbox),
                                    GTK_LIST_BOX_ROW (selected_application_row),
                                    channel);
}

static void
listbox_remove_all (GtkWidget *widget, gpointer user_data)
{
    GtkWidget *container = user_data;
    gtk_container_remove (GTK_CONTAINER (container), widget);
}

static void
xfce4_notifyd_known_applications_changed (XfconfChannel *channel,
                               const gchar *property,
                               const GValue *value,
                               gpointer user_data)
{
    GtkWidget *known_applications_listbox = user_data;
    GtkWidget *hbox;
    GtkWidget *label;
    GtkWidget *icon;
    GtkWidget *mute_switch;
    GtkCallback func = listbox_remove_all;
    GPtrArray *known_applications;
    GPtrArray *muted_applications;
    GValue *known_application;
    guint i, j;
    const gchar *icon_name;

    known_applications = xfconf_channel_get_arrayv (channel, KNOWN_APPLICATIONS_PROP);
    muted_applications = xfconf_channel_get_arrayv (channel, MUTED_APPLICATIONS_PROP);

    /* TODO: Check the old list versus the new one and only add/remove rows
             as needed instead instead of cleaning up the whole widget */
    /* Clean up the list and re-fill it */
    gtk_container_foreach (GTK_CONTAINER (known_applications_listbox), func, known_applications_listbox);

    if (known_applications != NULL) {
        for (i = 0; i < known_applications->len; i++) {
            GdkPixbuf *pix = NULL;
            GtkIconInfo *icon_info = NULL;
            GtkIconInfo *icon_info_lower = NULL;
            gchar const *desktop_icon_name = NULL;
            gchar *icon_name_lower;

            known_application = g_ptr_array_index (known_applications, i);
            hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
            label = gtk_label_new (g_value_get_string (known_application));
            /* Make sure spaces are converted to dashes so GTK_ICON_LOOKUP_GENERIC_FALLBACK works as expected */
            icon_name = g_strdelimit ((gchar *) g_value_get_string (known_application)," ",'-');
            icon_name_lower = g_ascii_strdown (icon_name, -1);
            icon_info = gtk_icon_theme_lookup_icon (gtk_icon_theme_get_default(), icon_name, 24, GTK_ICON_LOOKUP_GENERIC_FALLBACK);
            icon_info_lower = gtk_icon_theme_lookup_icon (gtk_icon_theme_get_default(), g_ascii_strdown (icon_name, -1), 24, GTK_ICON_LOOKUP_GENERIC_FALLBACK);
            desktop_icon_name = notify_icon_name_from_desktop_id (icon_name_lower);
            /* Find icons in the right priority: normal icon name with fallback, lowercase icon name with fallback,
               Desktop file icon property or empty. */
            if (icon_info) {
                pix = gtk_icon_info_load_icon (icon_info, NULL);
                icon = gtk_image_new_from_pixbuf (gdk_pixbuf_scale_simple (pix, 24, 24, GDK_INTERP_BILINEAR));
            }
            else if (icon_info_lower) {
                pix = gtk_icon_info_load_icon (icon_info_lower, NULL);
                icon = gtk_image_new_from_pixbuf (gdk_pixbuf_scale_simple (pix, 24, 24, GDK_INTERP_BILINEAR));
            }
            else if (desktop_icon_name)
                icon = gtk_image_new_from_icon_name (desktop_icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
            else
                icon = gtk_image_new ();
            g_free (icon_name_lower);
            if (pix)
                g_object_unref (G_OBJECT (pix));
            gtk_image_set_pixel_size (GTK_IMAGE (icon), 24);

#if GTK_CHECK_VERSION (3, 16, 0)
            gtk_label_set_xalign (GTK_LABEL (label), 0);
#else
            gtk_widget_set_halign (label, GTK_ALIGN_START);
#endif
            mute_switch = gtk_switch_new ();
            gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
            gtk_widget_set_valign (mute_switch, GTK_ALIGN_CENTER);
            gtk_switch_set_active (GTK_SWITCH (mute_switch), TRUE);
            gtk_box_pack_start (GTK_BOX (hbox), icon, FALSE, TRUE, 3);
            gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 3);
            gtk_box_pack_end (GTK_BOX (hbox), mute_switch, FALSE, TRUE, 3);
            gtk_list_box_insert (GTK_LIST_BOX (known_applications_listbox), hbox, -1);
            /* Set correct initial value as to whether an application is muted */
            if (muted_applications != NULL) {
                for (j = 0; j < muted_applications->len; j++) {
                    GValue *muted_application;
                    muted_application = g_ptr_array_index (muted_applications, j);
                    if (g_str_match_string (g_value_get_string (muted_application), g_value_get_string (known_application), FALSE) == TRUE) {
                        gtk_switch_set_active (GTK_SWITCH (mute_switch), FALSE);
                        break;
                    }
                    else
                        continue;
                }
            }
            g_signal_connect (G_OBJECT (mute_switch), "state-set", G_CALLBACK (xfce4_notifyd_switch_activated), channel);
        }
    }
    xfconf_array_free (known_applications);
    xfconf_array_free (muted_applications);
    gtk_widget_show_all (known_applications_listbox);
}

static void
xfce4_notifyd_do_not_disturb_activated (GtkSwitch *do_not_disturb_switch,
                                        gboolean state,
                                        gpointer user_data)
{
    GtkWidget *do_not_disturb_info = user_data;

    gtk_revealer_set_reveal_child (GTK_REVEALER (do_not_disturb_info),
                                    gtk_switch_get_active (GTK_SWITCH (do_not_disturb_switch)));
    gtk_switch_set_active (GTK_SWITCH (do_not_disturb_switch), state);
}

static void
xfce4_notifyd_do_fadeout_activated (GtkSwitch *do_fadeout,
                                    gboolean state,
                                    gpointer user_data)
{
    NotificationSlideoutWidgets *slideout_widgets = user_data;

    /* The sliding out animation is only available along with fade-out */
    if (state == FALSE)
        gtk_switch_set_active (GTK_SWITCH (slideout_widgets->do_slideout), FALSE);
    gtk_widget_set_sensitive (slideout_widgets->do_slideout, state);
    gtk_widget_set_sensitive (slideout_widgets->do_slideout_label, state);
    gtk_switch_set_active (GTK_SWITCH (do_fadeout), state);
}

static void
xfce4_notifyd_log_activated (GtkSwitch *log_switch,
                             gboolean state,
                             gpointer user_data)
{
    NotificationLogWidgets *log_widgets = user_data;
    const char *format = _("<b>Currently only urgent notifications are shown.</b>\nNotification logging is \%s.");
    char *markup;

    gtk_switch_set_state (GTK_SWITCH (log_switch), state);
    gtk_widget_set_sensitive (GTK_WIDGET (log_widgets->log_level), state);
    gtk_widget_set_sensitive (GTK_WIDGET (log_widgets->log_level_apps), state);
    gtk_widget_set_sensitive (GTK_WIDGET (log_widgets->log_level_apps_label), state);
    markup = g_markup_printf_escaped (format, state ? _("enabled") : _("disabled"));
    gtk_label_set_markup (GTK_LABEL (log_widgets->infobar_label), markup);
    g_free (markup);
}

static void
xfce4_notifyd_log_open (GtkButton *button, gpointer user_data) {
    gchar *notify_log_path;

    notify_log_path = xfce_resource_lookup (XFCE_RESOURCE_CACHE, XFCE_NOTIFY_LOG_FILE);
    if (notify_log_path) {
        gchar *uri = g_strdup_printf ("file://%s", notify_log_path);
        if (!g_app_info_launch_default_for_uri (uri, NULL, NULL))
            g_warning ("Could not open the log file: %s", notify_log_path);
        g_free (uri);
        g_free (notify_log_path);
    }
}

static void
xfce4_notifyd_log_populate (NotificationLogWidgets *log_widgets)
{
    GtkWidget *log_listbox = log_widgets->log_listbox;
    GKeyFile *notify_log;
    GtkCallback func = listbox_remove_all;
    gint i;
    GDateTime *today;
    gchar *timestamp;
    gchar *limit_label;
    GtkWidget *limit_button;
    gsize num_groups = 0;
    GdkPixbuf *pixbuf = NULL;
    gchar *notify_log_icon_folder;
    gchar *notify_log_icon_path;

    today = g_date_time_new_now_local ();
    timestamp = g_date_time_format (today, "%F");

    gtk_container_foreach (GTK_CONTAINER (log_listbox), func, log_listbox);
    notify_log = xfce_notify_log_get();
    notify_log_icon_folder = xfce_resource_save_location (XFCE_RESOURCE_CACHE,
                                                          XFCE_NOTIFY_ICON_PATH, TRUE);

    if (notify_log) {
        gchar **groups;
        gboolean yesterday = FALSE;
        int log_length;

        groups = g_key_file_get_groups (notify_log, &num_groups);
        log_length = GPOINTER_TO_UINT(num_groups) - LOG_DISPLAY_LIMIT;
        if (log_length < 0)
            log_length = 0;

        /* Notifications are only shown until LOG_DISPLAY_LIMIT is hit */
        for (i = log_length; groups && groups[i]; i += 1) {
            GtkWidget *grid;
            GtkWidget *summary, *body, *app_icon;
            const gchar *group = groups[i];
            const char *format = "<b>\%s</b>";
            const char *tooltip_format_simple = "<b>\%s</b> - \%s";
            char *markup;
            gchar *app_name;
            gchar *tooltip_timestamp = NULL;
            gchar *tmp;
            GDateTime *log_timestamp;
            GDateTime *local_timestamp = NULL;

            log_timestamp = g_date_time_new_from_iso8601 (group, NULL);
            if (log_timestamp != NULL)
            {
                local_timestamp = g_date_time_to_local (log_timestamp);
                g_date_time_unref (log_timestamp);
            }

            if (local_timestamp != NULL) {
                tooltip_timestamp = g_date_time_format (local_timestamp, "%c");
                g_date_time_unref (local_timestamp);
            }

            if (g_ascii_strncasecmp (timestamp, group, 10) == 0 && yesterday == FALSE) {
                GtkWidget *header;
                header = gtk_label_new (_("Yesterday and before"));
                gtk_widget_set_sensitive (header, FALSE);
                gtk_widget_set_margin_top (header, 3);
                gtk_widget_set_margin_bottom (header, 3);
                gtk_list_box_insert (GTK_LIST_BOX (log_listbox), header, 0);
                yesterday = TRUE;
            }

            app_name = g_key_file_get_string (notify_log, group, "app_name", NULL);
            tmp = g_key_file_get_string (notify_log, group, "summary", NULL);
            markup = g_markup_printf_escaped (format, tmp);
            g_free (tmp);
            summary = gtk_label_new (NULL);
            gtk_label_set_markup (GTK_LABEL (summary), markup);
#if GTK_CHECK_VERSION (3, 16, 0)
            gtk_label_set_xalign (GTK_LABEL (summary), 0);
#else
            gtk_widget_set_halign (summary, GTK_ALIGN_START);
#endif
            g_free (markup);
            tmp = g_key_file_get_string (notify_log, group, "body", NULL);
            body = gtk_label_new (NULL);
            gtk_label_set_markup (GTK_LABEL (body), tmp);
            if (g_strcmp0 (gtk_label_get_text (GTK_LABEL (body)), "") == 0) {
                gchar *tmp1;

                tmp1 = g_markup_escape_text (tmp, -1);
                gtk_label_set_text (GTK_LABEL (body), tmp1);
                g_free (tmp1);
            }
            g_free (tmp);
#if GTK_CHECK_VERSION (3, 16, 0)
            gtk_label_set_xalign (GTK_LABEL (body), 0);
#else
            gtk_widget_set_halign (body, GTK_ALIGN_START);
#endif
            gtk_label_set_ellipsize (GTK_LABEL (body), PANGO_ELLIPSIZE_END);
            tmp = g_key_file_get_string (notify_log, group, "app_icon", NULL);
            notify_log_icon_path = g_strconcat (notify_log_icon_folder , tmp, ".png", NULL);
            if (g_file_test (notify_log_icon_path, G_FILE_TEST_EXISTS)) {
                pixbuf = gdk_pixbuf_new_from_file_at_scale (notify_log_icon_path,
                                                            24, 24, FALSE, NULL);
                app_icon = gtk_image_new_from_pixbuf (pixbuf);
                if (pixbuf)
                    g_object_unref (pixbuf);
            } else {
                app_icon = gtk_image_new_from_icon_name (tmp, GTK_ICON_SIZE_LARGE_TOOLBAR);
                gtk_image_set_pixel_size (GTK_IMAGE (app_icon), 24);
            }
            g_free (notify_log_icon_path);
            g_free (tmp);
            gtk_widget_set_margin_start (app_icon, 3);
            // TODO: actions and timeout are missing (timeout is only interesting for urgent messages) - do we need that?
            grid = gtk_grid_new ();
            gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
            gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (app_icon), 0, 0 , 1, 2);

            /* Handle icon-only notifications */
            tmp = g_key_file_get_string (notify_log, group, "body", NULL);
            if (g_strcmp0 (tmp, "") == 0) {
                gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (summary), 1, 0, 1, 2);
                if (tooltip_timestamp != NULL) {
                    markup = g_strdup_printf (tooltip_format_simple, app_name, tooltip_timestamp);
                }
                else {
                    markup = g_strdup_printf (format, app_name);
                }
            }
            else {
                gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (summary), 1, 0, 1, 1);
                gtk_grid_attach (GTK_GRID (grid), GTK_WIDGET (body), 1, 1, 1, 1);
                if (tooltip_timestamp != NULL) {
                    markup = g_strdup_printf (tooltip_format_simple, app_name, tooltip_timestamp);
                }
                else {
                    markup = g_strdup_printf (format, app_name);
                }
            }
            g_free (tmp);
            g_free (app_name);
            g_free (tooltip_timestamp);

            gtk_widget_set_tooltip_markup (grid, markup);
            g_free (markup);

            gtk_list_box_insert (GTK_LIST_BOX (log_listbox), grid, 0);
        }
        if (log_length > 0) {
            limit_label = g_strdup_printf("Showing %d of %d notifications. Click to open full log.",
                                          LOG_DISPLAY_LIMIT, GPOINTER_TO_UINT(num_groups));
            limit_button = gtk_button_new_with_label (limit_label);
            gtk_widget_set_margin_bottom (limit_button, 3);
            gtk_widget_set_margin_top (limit_button, 3);
            gtk_button_set_relief (GTK_BUTTON (limit_button), GTK_RELIEF_NONE);
            g_signal_connect (G_OBJECT (limit_button), "clicked",
                              G_CALLBACK (xfce4_notifyd_log_open), log_listbox);
            gtk_list_box_insert (GTK_LIST_BOX (log_listbox), limit_button, LOG_DISPLAY_LIMIT + 1);
        }
        g_strfreev (groups);
        g_key_file_free (notify_log);
    }

    g_free (notify_log_icon_folder);
    g_date_time_unref (today);
    g_free (timestamp);

    gtk_widget_show_all (log_listbox);

    /* Update "open file" and "clear log" buttons sensitivity */
    gtk_widget_set_sensitive (GTK_WIDGET (gtk_toolbar_get_nth_item (log_widgets->log_toolbar, 1)), notify_log != NULL);
    gtk_widget_set_sensitive (GTK_WIDGET (gtk_toolbar_get_nth_item (log_widgets->log_toolbar, 2)), num_groups > 0);
}

static void
xfce4_notifyd_log_refresh (GtkButton *button, gpointer user_data) {
    xfce4_notifyd_log_populate (user_data);
}

static void xfce_notify_log_clear_button_clicked (GtkButton *button, gpointer user_data) {
    GtkWidget *log_listbox = ((NotificationLogWidgets *) user_data)->log_listbox;
    GtkCallback func = listbox_remove_all;
    GtkWidget *dialog;

    dialog = xfce_notify_clear_log_dialog ();
    gint result = gtk_dialog_run (GTK_DIALOG (dialog));
    /* Clear the listbox widget too in case the log is cleared */
    if (result == GTK_RESPONSE_OK)
        gtk_container_foreach (GTK_CONTAINER (log_listbox), func, log_listbox);
    gtk_widget_destroy (dialog);
}

static void xfce4_notifyd_show_help(GtkButton *button,
                                    GtkWidget *dialog)
{
    xfce_dialog_show_help_with_version(GTK_WINDOW(dialog), "notifyd", "start", NULL, NULL);
}

static void xfce_notify_bus_name_appeared_cb (GDBusConnection *connection,
                                              const gchar *name,
                                              const gchar *name_owner,
                                              gpointer user_data)
{
    GtkWidget  *notifyd_running = user_data;

    gtk_revealer_set_reveal_child (GTK_REVEALER (notifyd_running), FALSE);
}

static void xfce_notify_bus_name_vanished_cb (GDBusConnection *connection,
                                              const gchar *name,
                                              gpointer user_data)
{
    GtkWidget *notifyd_running = user_data;

    gtk_revealer_set_reveal_child (GTK_REVEALER (notifyd_running), TRUE);
}

GtkWidget *
placeholder_label_new (gchar *place_holder_text)
{
    GtkWidget *label;
    label = gtk_label_new ("");
    gtk_label_set_markup (GTK_LABEL (label), place_holder_text);
    gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);
    gtk_widget_set_sensitive (label, FALSE);
    gtk_widget_set_margin_start (label, 24);
    gtk_widget_set_margin_end (label, 24);
    gtk_widget_set_margin_top (label, 24);
    gtk_widget_set_margin_bottom (label, 24);
    return label;
}

static GtkWidget *
xfce4_notifyd_config_setup_dialog(GtkBuilder *builder)
{
    XfconfChannel *channel;
    GtkWidget *dlg;
    GtkWidget *btn;
    GtkWidget *sbtn;
    GtkWidget *slider;
    GtkWidget *theme_combo;
    GtkWidget *position_combo;
    GtkWidget *help_button;
    GtkWidget *known_applications_scrolled_window;
    GtkWidget *known_applications_listbox;
    GtkWidget *placeholder_label;
    GtkWidget *do_not_disturb_switch;
    GtkWidget *do_not_disturb_info;
    GtkWidget *log_switch;
    GtkWidget *log_scrolled_window;
    GtkToolItem *log_clear_button;
    GtkToolItem *log_refresh_button;
    GtkToolItem *log_open_button;
    GtkWidget *icon;
    GtkWidget *primary_monitor;
    GtkWidget *do_fadeout;
    GtkAdjustment *adj;
    GError *error = NULL;
    gchar *current_theme;
    NotificationLogWidgets log_widgets;
    NotificationSlideoutWidgets slideout_widgets;

    gtk_builder_connect_signals(builder, NULL);

    dlg = GTK_WIDGET(gtk_builder_get_object(builder, "notifyd_settings_dlg"));
    g_signal_connect(G_OBJECT(dlg), "response",
                     G_CALLBACK(xfce_notifyd_config_dialog_response), NULL);

    btn = GTK_WIDGET(gtk_builder_get_object(builder, "close_btn"));
    g_signal_connect_swapped(G_OBJECT(btn), "clicked",
                             G_CALLBACK(gtk_dialog_response), dlg);

    help_button = GTK_WIDGET(gtk_builder_get_object(builder, "help_btn"));
    g_signal_connect(G_OBJECT(help_button), "clicked",
                    G_CALLBACK(xfce4_notifyd_show_help), dlg);

    if(!xfconf_init(&error)) {
        xfce_message_dialog(NULL, _("Xfce Notify Daemon"),
                            "dialog-error",
                            _("Settings daemon is unavailable"),
                            error->message,
                            "application-exit", GTK_RESPONSE_ACCEPT,
                            NULL);
        exit(EXIT_FAILURE);
    }

    channel = xfconf_channel_new("xfce4-notifyd");

    /**************
        GENERAL   *
     **************/
    // Behavior
    do_not_disturb_switch = GTK_WIDGET (gtk_builder_get_object (builder, "do_not_disturb"));
    xfconf_g_property_bind (channel, "/do-not-disturb", G_TYPE_BOOLEAN,
                            G_OBJECT (do_not_disturb_switch), "active");
    /* Manually control the revealer for the infobar because of https://bugzilla.gnome.org/show_bug.cgi?id=710888 */
    do_not_disturb_info = GTK_WIDGET (gtk_builder_get_object (builder, "do_not_disturb_info"));
    gtk_revealer_set_reveal_child (GTK_REVEALER (do_not_disturb_info),
                                   gtk_switch_get_active (GTK_SWITCH (do_not_disturb_switch)));
    g_signal_connect (G_OBJECT (do_not_disturb_switch), "state-set",
                      G_CALLBACK (xfce4_notifyd_do_not_disturb_activated), do_not_disturb_info);

    primary_monitor = GTK_WIDGET(gtk_builder_get_object(builder, "primary_monitor"));
    xfconf_g_property_bind(channel, "/primary-monitor", G_TYPE_UINT,
                           G_OBJECT(primary_monitor), "active");
    if(gtk_combo_box_get_active(GTK_COMBO_BOX(primary_monitor)) == -1)
        gtk_combo_box_set_active(GTK_COMBO_BOX(primary_monitor), 0);

    // Appearance
    theme_combo = GTK_WIDGET(gtk_builder_get_object(builder, "theme_combo"));
    current_theme = xfconf_channel_get_string(channel, "/theme", "Default");
    xfce4_notifyd_config_setup_theme_combo(theme_combo, current_theme);
    g_free(current_theme);
    g_signal_connect(G_OBJECT(theme_combo), "changed",
                     G_CALLBACK(xfce4_notifyd_config_theme_combo_changed),
                     channel);
    g_signal_connect(G_OBJECT(channel), "property-changed::/theme",
                     G_CALLBACK(xfce4_notifyd_config_theme_changed),
                     theme_combo);

    position_combo = GTK_WIDGET(gtk_builder_get_object(builder, "position_combo"));
    xfconf_g_property_bind(channel, "/notify-location", G_TYPE_UINT,
                           G_OBJECT(position_combo), "active");
    if(gtk_combo_box_get_active(GTK_COMBO_BOX(position_combo)) == -1)
        gtk_combo_box_set_active(GTK_COMBO_BOX(position_combo), GTK_CORNER_TOP_RIGHT);

    slider = GTK_WIDGET(gtk_builder_get_object(builder, "opacity_slider"));
    g_signal_connect(G_OBJECT(slider), "format-value",
                     G_CALLBACK(xfce4_notifyd_slider_format_value), NULL);
    adj = gtk_range_get_adjustment(GTK_RANGE(slider));
    xfconf_g_property_bind(channel, "/initial-opacity", G_TYPE_DOUBLE,
                           G_OBJECT(adj), "value");

    sbtn = GTK_WIDGET(gtk_builder_get_object(builder, "expire_timeout_sbtn"));
    xfconf_g_property_bind(channel, "/expire-timeout", G_TYPE_INT,
                           G_OBJECT(sbtn), "value");

    do_fadeout = GTK_WIDGET(gtk_builder_get_object(builder, "do_fadeout"));
    gtk_switch_set_active (GTK_SWITCH (do_fadeout), TRUE);
    xfconf_g_property_bind(channel, "/do-fadeout", G_TYPE_BOOLEAN,
                           G_OBJECT(do_fadeout), "active");

    slideout_widgets.do_slideout_label = GTK_WIDGET(gtk_builder_get_object(builder, "do_slideout_label"));
    slideout_widgets.do_slideout = GTK_WIDGET(gtk_builder_get_object(builder, "do_slideout"));
    xfconf_g_property_bind(channel, "/do-slideout", G_TYPE_BOOLEAN,
                           G_OBJECT(slideout_widgets.do_slideout), "active");
    g_signal_connect (G_OBJECT (do_fadeout), "state-set",
                      G_CALLBACK (xfce4_notifyd_do_fadeout_activated), &slideout_widgets);
    if (gtk_switch_get_active (GTK_SWITCH (do_fadeout)) == FALSE) {
        gtk_widget_set_sensitive (slideout_widgets.do_slideout_label, FALSE);
        gtk_widget_set_sensitive (slideout_widgets.do_slideout, FALSE);
    }

    btn = GTK_WIDGET(gtk_builder_get_object(builder, "preview_button"));
    g_signal_connect(G_OBJECT(btn), "clicked",
                     G_CALLBACK(xfce_notifyd_config_preview_clicked), NULL);

    /*******************
        APPLICATIONS   *
     *******************/
    known_applications_scrolled_window = GTK_WIDGET (gtk_builder_get_object (builder, "known_applications_scrolled_window"));
    known_applications_listbox = gtk_list_box_new ();
    gtk_container_add (GTK_CONTAINER (known_applications_scrolled_window), known_applications_listbox);
    gtk_list_box_set_header_func (GTK_LIST_BOX (known_applications_listbox), display_header_func, NULL, NULL);

    placeholder_label = placeholder_label_new (_("<big><b>Currently there are no known applications.</b></big>"
                                                 "\nAs soon as an application sends a notification"
                                                 "\nit will appear in this list."));
    /* Initialize the list of known applications */
    xfce4_notifyd_known_applications_changed (channel, KNOWN_APPLICATIONS_PROP, NULL, known_applications_listbox);
    gtk_list_box_set_placeholder (GTK_LIST_BOX (known_applications_listbox), placeholder_label);
    gtk_widget_show_all (placeholder_label);
    g_signal_connect (G_OBJECT (known_applications_listbox), "row-activated",
                      G_CALLBACK (xfce4_notifyd_row_activated),
                      channel);
    g_signal_connect (G_OBJECT (channel),
                      "property-changed::" KNOWN_APPLICATIONS_PROP,
                      G_CALLBACK (xfce4_notifyd_known_applications_changed), known_applications_listbox);

    /**********
        LOG   *
     **********/
    log_widgets.log_level = GTK_WIDGET (gtk_builder_get_object (builder, "log_level"));
    log_widgets.log_level_apps = GTK_WIDGET (gtk_builder_get_object (builder, "log_level_apps"));
    log_widgets.log_level_apps_label = GTK_WIDGET (gtk_builder_get_object (builder, "log_level_apps_label"));
    log_widgets.infobar_label = GTK_WIDGET (gtk_builder_get_object (builder, "infobar_label"));
    log_switch = GTK_WIDGET (gtk_builder_get_object (builder, "log_switch"));
    xfconf_g_property_bind (channel, "/notification-log", G_TYPE_BOOLEAN,
                            G_OBJECT (log_switch), "active");
    g_signal_connect (G_OBJECT (log_switch), "state-set",
                      G_CALLBACK (xfce4_notifyd_log_activated), &log_widgets);
    xfconf_g_property_bind(channel, "/log-level", G_TYPE_UINT,
                           G_OBJECT(log_widgets.log_level), "active");
    xfconf_g_property_bind(channel, "/log-level-apps", G_TYPE_UINT,
                          G_OBJECT(log_widgets.log_level_apps), "active");
    sbtn = GTK_WIDGET (gtk_builder_get_object (builder, "log_max_size"));
    xfconf_g_property_bind(channel, "/log-max-size", G_TYPE_UINT,
                           G_OBJECT(sbtn), "value");

    /* Initialize the settings' states correctly */
    if(gtk_combo_box_get_active(GTK_COMBO_BOX(log_widgets.log_level)) == -1)
        gtk_combo_box_set_active(GTK_COMBO_BOX(log_widgets.log_level), 0);
    if(gtk_combo_box_get_active(GTK_COMBO_BOX(log_widgets.log_level_apps)) == -1)
        gtk_combo_box_set_active(GTK_COMBO_BOX(log_widgets.log_level_apps), 0);
    xfce4_notifyd_log_activated (GTK_SWITCH (log_switch), gtk_switch_get_active (GTK_SWITCH(log_switch)), &log_widgets);

    log_scrolled_window = GTK_WIDGET (gtk_builder_get_object (builder, "log_scrolled_window"));
    log_widgets.log_listbox = gtk_list_box_new ();
    gtk_container_add (GTK_CONTAINER (log_scrolled_window), log_widgets.log_listbox);
    gtk_list_box_set_header_func (GTK_LIST_BOX (log_widgets.log_listbox), display_header_func, NULL, NULL);
    placeholder_label = placeholder_label_new (_("<big><b>Empty log</b></big>"
                                                 "\nNo notifications have been logged yet."));
    gtk_list_box_set_placeholder (GTK_LIST_BOX (log_widgets.log_listbox), placeholder_label);
    gtk_widget_show_all (placeholder_label);

    log_widgets.log_toolbar = GTK_TOOLBAR (gtk_builder_get_object (builder, "log_toolbar"));
    icon = gtk_image_new_from_icon_name ("view-refresh-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_image_set_pixel_size (GTK_IMAGE (icon), 16);
    log_refresh_button = gtk_tool_button_new (icon, _("Refresh"));
    gtk_widget_set_tooltip_text (GTK_WIDGET (log_refresh_button), _("Refresh the notification log"));
    gtk_toolbar_insert(log_widgets.log_toolbar, GTK_TOOL_ITEM(log_refresh_button), 0);
    g_signal_connect (G_OBJECT (log_refresh_button), "clicked",
                      G_CALLBACK (xfce4_notifyd_log_refresh), &log_widgets);
    icon = gtk_image_new_from_icon_name ("document-open-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_image_set_pixel_size (GTK_IMAGE (icon), 16);
    log_open_button = gtk_tool_button_new (icon, _("Open"));
    gtk_widget_set_tooltip_text (GTK_WIDGET (log_open_button), _("Open the notification log in an external editor"));
    gtk_toolbar_insert(log_widgets.log_toolbar, GTK_TOOL_ITEM(log_open_button), 1);
    g_signal_connect (G_OBJECT (log_open_button), "clicked",
                      G_CALLBACK (xfce4_notifyd_log_open), log_widgets.log_listbox);
    icon = gtk_image_new_from_icon_name ("edit-clear-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_image_set_pixel_size (GTK_IMAGE (icon), 16);
    log_clear_button = gtk_tool_button_new (icon, _("Clear"));
    gtk_widget_set_tooltip_text (GTK_WIDGET (log_clear_button), _("Clear the notification log"));
    gtk_toolbar_insert(log_widgets.log_toolbar, GTK_TOOL_ITEM(log_clear_button), 2);
    g_signal_connect (G_OBJECT (log_clear_button), "clicked",
                      G_CALLBACK (xfce_notify_log_clear_button_clicked), &log_widgets);
    gtk_widget_show_all (GTK_WIDGET(log_widgets.log_toolbar));

    xfce4_notifyd_log_populate (&log_widgets);

    return dlg;
}

int
main(int argc,
     char **argv)
{
    GtkWidget *settings_dialog = NULL;
    GtkWidget *notifyd_running;
    GtkBuilder *builder;
    gboolean opt_version = FALSE;
    gint32 opt_socket_id = 0;
    guint watch_handle_id;
    GOptionEntry option_entries[] = {
        { "version", 'V', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &opt_version, N_("Display version information"), NULL },
        { "socket-id", 's', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT, &opt_socket_id, N_("Settings manager socket"), N_("SOCKET_ID") },
        { NULL, },
    };
    GError *error = NULL;

    xfce_textdomain(GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");

    if(!gtk_init_with_args(&argc, &argv, NULL, option_entries, PACKAGE, &error)) {
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
        g_print("Copyright (c) 2010 Brian Tarricone <bjt23@cornell.edu>\n");
        g_print("Copyright (c) 2010 Jérôme Guelfucci <jeromeg@xfce.org>\n");
        g_print("Copyright (c) 2016 Ali Abdallah <ali@xfce.org>\n");
        g_print("Copyright (c) 2016 Simon Steinbeiß <simon@xfce.org>\n");
        g_print(_("Released under the terms of the GNU General Public License, version 2\n"));
        g_print(_("Please report bugs to %s.\n"), PACKAGE_BUGREPORT);

        return EXIT_SUCCESS;
    }

    if (!notify_init ("Xfce4-notifyd settings")) {
      g_error ("Failed to initialize libnotify.");
      return EXIT_FAILURE;
    }

    builder = gtk_builder_new();
    gtk_builder_add_from_string(builder, xfce4_notifyd_config_ui, xfce4_notifyd_config_ui_length, NULL);
    if(G_UNLIKELY(!builder)) {
        g_error("Unable to read embedded UI definition file");
        return EXIT_FAILURE;
    }

    settings_dialog = xfce4_notifyd_config_setup_dialog(builder);

    notifyd_running = GTK_WIDGET (gtk_builder_get_object (builder, "notifyd_running"));
    gtk_revealer_set_reveal_child (GTK_REVEALER (notifyd_running), FALSE);
    watch_handle_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                        "org.freedesktop.Notifications",
                                        G_BUS_NAME_WATCHER_FLAGS_NONE,
                                        xfce_notify_bus_name_appeared_cb,
                                        xfce_notify_bus_name_vanished_cb,
                                        notifyd_running,
                                        NULL);

    if(opt_socket_id) {
        GtkWidget *plug, *plug_child;

        plug = gtk_plug_new(opt_socket_id);
        gtk_widget_show(plug);
        g_signal_connect(G_OBJECT(plug), "delete-event",
                         G_CALLBACK(gtk_main_quit), NULL);

        plug_child = GTK_WIDGET(gtk_builder_get_object(builder, "plug-child"));

        /* In the glade file, plug_child has parent, so remove it first */
        gtk_container_remove (GTK_CONTAINER(gtk_widget_get_parent(plug_child)), plug_child);
        gtk_container_add (GTK_CONTAINER(plug), plug_child);

        gdk_notify_startup_complete();
        g_object_unref(G_OBJECT(builder));
        gtk_widget_destroy(settings_dialog);

        gtk_main();
    } else {
        g_object_unref(G_OBJECT(builder));

        gtk_dialog_run(GTK_DIALOG(settings_dialog));
    }

    g_bus_unwatch_name (watch_handle_id);
    notify_uninit();
    xfconf_shutdown();

    return EXIT_SUCCESS;
}
