/*
 *  xfce4-notifyd
 *
 *  Copyright (c) 2008,2023 Brian Tarricone <brian@tarricone.org>
 *  Copyright (c) Simon Steinbeiß <simon@xfce.org>
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

#include <glib/gi18n-lib.h>

#include <libxfce4ui/libxfce4ui.h>

#include "common/xfce-notify-common.h"
#include "common/xfce-notify-log-util.h"
#include "xfce-notify-log-viewer.h"

#define LOG_DISPLAY_LIMIT             100

#define LOG_ENTRY_KEY  "xfce4-notify-log-entry"
#define LOG_IMAGE_KEY  "xfce4-notify-log-image"

#ifndef P_
#define P_(singular, plural, n)  ngettext(singular, plural, n)
#endif

typedef struct _XfceNotifyLogViewer {
    GtkBox parent;

    XfconfChannel *channel;
    XfceNotifyLog *log;

    GtkWidget *scroller;
    GtkWidget *listbox;
    GtkWidget *last_clicked_row;
    GtkWidget *loading_row;

    GtkWidget *toolbar;
    GtkToolItem *clear_log_button;
    GtkToolItem *mark_read_button;

    gboolean yesterday_header_added;
    gchar *last_entry_id;
    guint load_items_id;
} XfceNotifyLogViewer;

enum {
    PROP0,
    PROP_CHANNEL,
    PROP_LOG,
};

static void xfce_notify_log_viewer_constructed(GObject *obj);
static void xfce_notify_log_viewer_set_property(GObject *obj,
                                                guint prop_id,
                                                const GValue *value,
                                                GParamSpec *pspec);
static void xfce_notify_log_viewer_get_property(GObject *obj,
                                                guint prop_id,
                                                GValue *value,
                                                GParamSpec *pspec);
static void xfce_notify_log_viewer_dispose(GObject *obj);
static void xfce_notify_log_viewer_finalize(GObject *obj);

static void xfce_notify_log_viewer_populate(XfceNotifyLogViewer *viewer);
static void xfce_notify_log_viewer_refresh(XfceNotifyLogViewer *viewer);

static void xfce_notify_log_viewer_clear(XfceNotifyLogViewer *viewer);
static void xfce_notify_log_viewer_mark_read(XfceNotifyLogViewer *viewer);

static void xfce_notify_log_viewer_log_changed(XfceNotifyLogViewer *viewer);

static void xfce_notify_log_viewer_listbox_display_header_func(GtkListBoxRow *row,
                                                               GtkListBoxRow *before,
                                                               gpointer user_data);
static gboolean xfce_notify_log_viewer_listbox_button_press(GtkWidget *listbox,
                                                            GdkEventButton *evt,
                                                            XfceNotifyLogViewer *viewer);

static void xfce_notify_log_viewer_scroll_edge_reached(XfceNotifyLogViewer *viewer,
                                                       GtkPositionType pos);


G_DEFINE_TYPE(XfceNotifyLogViewer, xfce_notify_log_viewer, GTK_TYPE_BOX)


static void
xfce_notify_log_viewer_class_init(XfceNotifyLogViewerClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->constructed = xfce_notify_log_viewer_constructed;
    gobject_class->set_property = xfce_notify_log_viewer_set_property;
    gobject_class->get_property = xfce_notify_log_viewer_get_property;
    gobject_class->dispose = xfce_notify_log_viewer_dispose;
    gobject_class->finalize = xfce_notify_log_viewer_finalize;

    g_object_class_install_property(gobject_class,
                                    PROP_CHANNEL,
                                    g_param_spec_object("channel",
                                                        "channel",
                                                        "settings channel",
                                                        XFCONF_TYPE_CHANNEL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class,
                                    PROP_LOG,
                                    g_param_spec_object("log",
                                                        "log",
                                                        "XfceNotifyLog instance",
                                                        XFCE_TYPE_NOTIFY_LOG,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
xfce_notify_log_viewer_init(XfceNotifyLogViewer *viewer) {}

static void
xfce_notify_log_viewer_constructed(GObject *obj) {
    XfceNotifyLogViewer *viewer = XFCE_NOTIFY_LOG_VIEWER(obj);
    GtkWidget *placeholder_label;
    GtkWidget *icon;
    GtkToolItem *button;
    gint icon_width, icon_height, icon_size;

    G_OBJECT_CLASS(xfce_notify_log_viewer_parent_class)->constructed(obj);

    gtk_icon_size_lookup(GTK_ICON_SIZE_SMALL_TOOLBAR, &icon_width, &icon_height);
    icon_size = MIN(icon_width, icon_height);

    viewer->scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(viewer), viewer->scroller, TRUE, TRUE, 0);
    gtk_widget_show(viewer->scroller);
    g_signal_connect_swapped(viewer->scroller, "edge-reached",
                             G_CALLBACK(xfce_notify_log_viewer_scroll_edge_reached), viewer);

    viewer->listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(viewer->listbox), GTK_SELECTION_MULTIPLE);
    gtk_list_box_set_header_func (GTK_LIST_BOX(viewer->listbox), xfce_notify_log_viewer_listbox_display_header_func, NULL, NULL);
    if (viewer->log == NULL) {
        placeholder_label = xfce_notify_create_placeholder_label(_("Unable to open notification log"));
    } else {
        placeholder_label = xfce_notify_create_placeholder_label(_("<big><b>Empty log</b></big>"
                                                                   "\nNo notifications have been logged yet."));
    }
    gtk_list_box_set_placeholder(GTK_LIST_BOX(viewer->listbox), placeholder_label);
    gtk_container_add(GTK_CONTAINER(viewer->scroller), viewer->listbox);
    gtk_widget_show(viewer->listbox);
    g_signal_connect(viewer->listbox, "button-press-event",
                     G_CALLBACK(xfce_notify_log_viewer_listbox_button_press), viewer);

    viewer->toolbar = gtk_toolbar_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(viewer->toolbar), GTK_STYLE_CLASS_INLINE_TOOLBAR);
    gtk_box_pack_end(GTK_BOX(viewer), viewer->toolbar, FALSE, FALSE, 0);
    gtk_widget_show(viewer->toolbar);

    icon = gtk_image_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), icon_size);
    button = gtk_tool_button_new(icon, _("Refresh"));
    gtk_widget_set_tooltip_text(GTK_WIDGET(button), _("Refresh the notification log"));
    gtk_toolbar_insert(GTK_TOOLBAR(viewer->toolbar), button, -1);
    g_signal_connect_swapped(G_OBJECT(button), "clicked",
                             G_CALLBACK(xfce_notify_log_viewer_refresh), viewer);

    icon = gtk_image_new_from_icon_name("edit-clear-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_image_set_pixel_size(GTK_IMAGE (icon), icon_size);
    button = gtk_tool_button_new(icon, _("Clear"));
    gtk_widget_set_tooltip_text(GTK_WIDGET(button), _("Clear the notification log"));
    gtk_widget_set_sensitive(GTK_WIDGET(button), viewer->log != NULL);
    gtk_toolbar_insert(GTK_TOOLBAR(viewer->toolbar), GTK_TOOL_ITEM(button), -1);
    g_signal_connect_swapped(G_OBJECT(button), "clicked",
                             G_CALLBACK(xfce_notify_log_viewer_clear), viewer);
    viewer->clear_log_button = button;

    icon = gtk_image_new_from_icon_name("checkbox-checked-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), icon_size);
    button = gtk_tool_button_new(icon, _("Mark All Read"));
    gtk_widget_set_sensitive(GTK_WIDGET(button), viewer->log != NULL && xfce_notify_log_has_unread_messages(viewer->log));
    gtk_widget_set_tooltip_text(GTK_WIDGET(button), _("Mark all unread notifications as read"));
    gtk_toolbar_insert(GTK_TOOLBAR(viewer->toolbar), GTK_TOOL_ITEM(button), -1);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(xfce_notify_log_viewer_mark_read), viewer);
    viewer->mark_read_button = button;

    if (viewer->log != NULL) {
        g_signal_connect_swapped(viewer->log, "changed",
                                 G_CALLBACK(xfce_notify_log_viewer_log_changed), viewer);
    }

    g_signal_connect_swapped(viewer->channel, "property-changed::" DATETIME_FORMAT_PROP,
                             G_CALLBACK(xfce_notify_log_viewer_populate), viewer);
    g_signal_connect_swapped(viewer->channel, "property-changed::" DATETIME_CUSTOM_FORMAT_PROP,
                             G_CALLBACK(xfce_notify_log_viewer_populate), viewer);

    xfce_notify_log_viewer_populate(viewer);
}

static void
xfce_notify_log_viewer_set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec) {
    XfceNotifyLogViewer *viewer = XFCE_NOTIFY_LOG_VIEWER(obj);

    switch (prop_id) {
        case PROP_CHANNEL:
            viewer->channel = g_value_dup_object(value);
            break;

        case PROP_LOG:
            if (g_value_get_object(value) != NULL) {
                viewer->log = g_value_dup_object(value);
            }
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
            break;
    }
}

static void
xfce_notify_log_viewer_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec) {
    XfceNotifyLogViewer *viewer = XFCE_NOTIFY_LOG_VIEWER(obj);

    switch (prop_id) {
        case PROP_CHANNEL:
            g_value_set_object(value, viewer->channel);
            break;

        case PROP_LOG:
            g_value_set_object(value, viewer->log);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
            break;
    }
}

static void
xfce_notify_log_viewer_dispose(GObject *obj) {
    XfceNotifyLogViewer *viewer = XFCE_NOTIFY_LOG_VIEWER(obj);

    if (viewer->load_items_id != 0) {
        g_source_remove(viewer->load_items_id);
        viewer->load_items_id = 0;
    }

    G_OBJECT_CLASS(xfce_notify_log_viewer_parent_class)->dispose(obj);
}

static void
xfce_notify_log_viewer_finalize(GObject *obj) {
    XfceNotifyLogViewer *viewer = XFCE_NOTIFY_LOG_VIEWER(obj);

    g_signal_handlers_disconnect_by_func(viewer->channel,
                                         xfce_notify_log_viewer_populate,
                                         viewer);
    g_object_unref(viewer->channel);

    if (viewer->log != NULL) {
        g_signal_handlers_disconnect_by_func(viewer->log,
                                             xfce_notify_log_viewer_log_changed,
                                             viewer);
        g_object_unref(viewer->log);
    }

    g_free(viewer->last_entry_id);

    G_OBJECT_CLASS(xfce_notify_log_viewer_parent_class)->finalize(obj);
}

static void
xfce_notify_log_viewer_listbox_display_header_func(GtkListBoxRow *row,
                                                   GtkListBoxRow *before,
                                                   gpointer user_data)
{
    if (G_LIKELY(before != NULL)) {
        GtkWidget *header = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_show(header);
        gtk_list_box_row_set_header(row, header);
    }
}

static cairo_surface_t *
notify_icon_for_entry(XfceNotifyLogEntry *entry, gint size, gint scale_factor, GdkRGBA *emblem_color) {
    cairo_surface_t *icon = notify_log_load_icon(xfce_notify_log_get_icon_folder(), entry->icon_id, entry->app_id, size, scale_factor);

    if (icon == NULL) {
        icon = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                          size * scale_factor,
                                          size * scale_factor);
        cairo_surface_set_device_scale(icon, scale_factor, scale_factor);
    }

    if (!entry->is_read && emblem_color != NULL) {
        notify_log_icon_add_unread_emblem(icon, emblem_color);
    }

    return icon;
}

static void
log_entry_mark_read_clicked(GtkWidget *mi, XfceNotifyLogViewer *viewer) {
    GList *selected = gtk_list_box_get_selected_rows(GTK_LIST_BOX(viewer->listbox));

    for (GList *l = selected; l != NULL; l = l->next) {
        GtkWidget *row = GTK_WIDGET(l->data);
        XfceNotifyLogEntry *entry = g_object_get_data(G_OBJECT(row), LOG_ENTRY_KEY);

        if (G_LIKELY(entry != NULL) && !entry->is_read) {
            GtkWidget *image;

            xfce_notify_log_mark_read(viewer->log, entry->id);
            entry->is_read = TRUE;

            image = g_object_get_data(G_OBJECT(row), LOG_IMAGE_KEY);
            if (G_LIKELY(image != NULL)) {
                cairo_surface_t *icon = NULL;

                g_object_get(image,
                             "surface", &icon,
                             NULL);
                if (G_LIKELY(icon != NULL)) {
                    gint scale_factor;
                    gint size;

                    scale_factor = gtk_widget_get_scale_factor(image);
                    size = MAX(cairo_image_surface_get_width(icon), cairo_image_surface_get_height(icon)) / scale_factor;
                    cairo_surface_destroy(icon);

                    icon = notify_icon_for_entry(entry, size, scale_factor, NULL);
                    gtk_image_set_from_surface(GTK_IMAGE(image), icon);
                    cairo_surface_destroy(icon);
                }
            }
        }
    }

    g_list_free(selected);
}

static void
log_entry_delete_clicked(GtkWidget *mi, XfceNotifyLogViewer *viewer) {
    GList *selected = gtk_list_box_get_selected_rows(GTK_LIST_BOX(viewer->listbox));

    for (GList *l = selected; l != NULL; l = l->next) {
        GtkWidget *row = GTK_WIDGET(l->data);
        XfceNotifyLogEntry *entry = g_object_get_data(G_OBJECT(row), LOG_ENTRY_KEY);

        if (entry != NULL) {
            xfce_notify_log_delete(viewer->log, entry->id);
            gtk_container_remove(GTK_CONTAINER(viewer->listbox), row);
        }
    }

    g_list_free(selected);
}

static void
set_last_clicked_row(XfceNotifyLogViewer *viewer, GtkWidget *row) {
    if (viewer->last_clicked_row != NULL) {
        g_object_remove_weak_pointer(G_OBJECT(viewer->last_clicked_row), (gpointer *)&viewer->last_clicked_row);
    }

    if (row != NULL) {
        viewer->last_clicked_row = row;
        g_object_add_weak_pointer(G_OBJECT(row), (gpointer *)&viewer->last_clicked_row);
    }
}

static gboolean
xfce_notify_log_viewer_listbox_row_button_press(GtkWidget *eventbox,
                                                GdkEventButton *evt,
                                                XfceNotifyLogViewer *viewer)
{
    GtkWidget *row = gtk_widget_get_parent(eventbox);

    g_return_val_if_fail(GTK_IS_LIST_BOX_ROW(row), FALSE);

    if (evt->type == GDK_BUTTON_PRESS) {
        if (evt->button == GDK_BUTTON_PRIMARY) {
            // I'm not sure why, but GtkListBox implements weird GTK_SELECTION_MULTIPLE
            // behavior where when you click different rows without holding any modifier
            // keys, more rows get selected (as if you were holding ctrl), and there's
            // no way to de-select rows once they're selected.

            GList *selected = gtk_list_box_get_selected_rows(GTK_LIST_BOX(viewer->listbox));
            gboolean have_selection = selected != NULL;
            g_list_free(selected);

            if (!have_selection) {
                gtk_list_box_select_row(GTK_LIST_BOX(viewer->listbox), GTK_LIST_BOX_ROW(row));
            } else if ((evt->state & GDK_SHIFT_MASK) != 0) {
                if (G_UNLIKELY(viewer->last_clicked_row == NULL)) {
                    gtk_list_box_select_row(GTK_LIST_BOX(viewer->listbox), GTK_LIST_BOX_ROW(row));
                } else {
                    GList *rows = gtk_container_get_children(GTK_CONTAINER(viewer->listbox));
                    gboolean start_selection = FALSE;
                    gboolean stop_selection = FALSE;

                    gtk_list_box_unselect_all(GTK_LIST_BOX(viewer->listbox));
                    for (GList *l = rows; l != NULL; l = l->next) {
                        GtkWidget *cur_row = GTK_WIDGET(l->data);

                        if (cur_row == row || cur_row == viewer->last_clicked_row) {
                            if (!start_selection) {
                                start_selection = TRUE;
                            } else {
                                stop_selection = TRUE;
                            }
                        }

                        if (start_selection) {
                            gtk_list_box_select_row(GTK_LIST_BOX(viewer->listbox), GTK_LIST_BOX_ROW(cur_row));
                        }

                        if (stop_selection) {
                            break;
                        }
                    }

                    g_list_free(rows);
                }
            } else if ((evt->state & GDK_CONTROL_MASK) != 0) {
                if (gtk_list_box_row_is_selected(GTK_LIST_BOX_ROW(row))) {
                    gtk_list_box_unselect_row(GTK_LIST_BOX(viewer->listbox), GTK_LIST_BOX_ROW(row));
                } else {
                    gtk_list_box_select_row(GTK_LIST_BOX(viewer->listbox), GTK_LIST_BOX_ROW(row));
                }
            } else {
                gtk_list_box_unselect_all(GTK_LIST_BOX(viewer->listbox));
                gtk_list_box_select_row(GTK_LIST_BOX(viewer->listbox), GTK_LIST_BOX_ROW(row));
            }

            set_last_clicked_row(viewer, row);

            return TRUE;
        } else if (evt->button == GDK_BUTTON_SECONDARY) {
            GList *selected;
            guint n_selected = 0;
            guint n_unread = 0;

            if ((evt->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == 0) {
                if (!gtk_list_box_row_is_selected(GTK_LIST_BOX_ROW(GTK_LIST_BOX_ROW(row)))) {
                    gtk_list_box_unselect_all(GTK_LIST_BOX(viewer->listbox));
                    gtk_list_box_select_row(GTK_LIST_BOX(viewer->listbox), GTK_LIST_BOX_ROW(row));
                    set_last_clicked_row(viewer, row);
                }
            }

            selected = gtk_list_box_get_selected_rows(GTK_LIST_BOX(viewer->listbox));
            for (GList *l = selected; l != NULL; l = l->next) {
                GtkWidget *sel_row = GTK_WIDGET(l->data);
                XfceNotifyLogEntry *entry = g_object_get_data(G_OBJECT(sel_row), LOG_ENTRY_KEY);
                if (entry != NULL && !entry->is_read) {
                    ++n_unread;
                }
                ++n_selected;
            }
            g_list_free(selected);

            if (n_selected > 0) {
                GtkWidget *menu;
                GtkWidget *mi;
                gchar *label;

                menu = gtk_menu_new();
                g_signal_connect(menu, "selection-done",
                                 G_CALLBACK(gtk_widget_destroy), NULL);

                if (n_unread == 0) {
                    label = g_strdup(_("Mark log entry _read"));
                } else {
                    label = g_strdup_printf(P_("Mark log entry _read", "Mark %d log entries _read", n_unread), n_unread);
                }
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
                mi = gtk_image_menu_item_new_with_mnemonic(label);
                gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi), gtk_image_new_from_icon_name("checkbox-checked-symbolic", GTK_ICON_SIZE_MENU));
G_GNUC_END_IGNORE_DEPRECATIONS
                gtk_widget_set_sensitive(mi, n_unread > 0);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
                g_signal_connect(mi, "activate",
                                 G_CALLBACK(log_entry_mark_read_clicked), viewer);
                g_free(label);

                label = g_strdup_printf(P_("_Delete log entry", "_Delete %d log entries", n_selected), n_selected);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
                mi = gtk_image_menu_item_new_with_mnemonic(label);
                gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi), gtk_image_new_from_icon_name("edit-delete-symbolic", GTK_ICON_SIZE_MENU));
G_GNUC_END_IGNORE_DEPRECATIONS
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
                g_signal_connect(mi, "activate",
                                 G_CALLBACK(log_entry_delete_clicked), viewer);
                g_free(label);

                gtk_widget_show_all(menu);

                gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)evt);

                return TRUE;
            }
        }
    }


    return FALSE;
}

// GtkListBox's selection behavior is bizarre, so we have to reimplement it (see above)
static gboolean
xfce_notify_log_viewer_listbox_button_press(GtkWidget *listbox, GdkEventButton *evt, XfceNotifyLogViewer *viewer) {
    GtkListBoxRow *row = gtk_list_box_get_row_at_y(GTK_LIST_BOX(listbox), evt->y);

    if (row != NULL) {
        GtkWidget *eventbox = gtk_bin_get_child(GTK_BIN(row));

        if (GTK_IS_EVENT_BOX(eventbox)) {
            gboolean ret;
            gint x, y;
            GdkEvent *copy;

            gtk_widget_translate_coordinates(listbox, eventbox, evt->x, evt->y, &x, &y);
            copy = gdk_event_copy((GdkEvent *)evt);
            copy->button.x = x;
            copy->button.y = y;
            if (copy->button.window != NULL) {
                g_object_unref(copy->button.window);
            }
            copy->button.window = g_object_ref(gtk_widget_get_window(eventbox));

            ret = xfce_notify_log_viewer_listbox_row_button_press(eventbox, &copy->button, viewer);
            gdk_event_free(copy);

            return ret;
        }
    }

    return FALSE;
}

static void
xfce_notify_log_viewer_add_entries(XfceNotifyLogViewer *viewer, GList *entries) {
    GDateTime *today;
    gint today_year, today_day;
    gint icon_width, icon_height, icon_size;
    XfceDateTimeFormat dt_format;
    gchar *custom_dt_format;
    gint scale_factor = gtk_widget_get_scale_factor(viewer->listbox);
    GdkRGBA emblem_color;

    gtk_icon_size_lookup(GTK_ICON_SIZE_LARGE_TOOLBAR, &icon_width, &icon_height);
    icon_size = MIN(icon_width, icon_height);

    gtk_style_context_get_color(gtk_widget_get_style_context(GTK_WIDGET(viewer)), GTK_STATE_FLAG_NORMAL, &emblem_color);

    today = g_date_time_new_now_local();
    today_year = g_date_time_get_year(today);
    today_day = g_date_time_get_day_of_year(today);

    dt_format = xfconf_channel_get_int(viewer->channel, DATETIME_FORMAT_PROP, XFCE_DATE_TIME_FORMAT_LOCALE);
    custom_dt_format = xfconf_channel_get_string(viewer->channel, DATETIME_CUSTOM_FORMAT_PROP, NULL);

    for (GList *l = entries; l != NULL; l = l->next) {
        XfceNotifyLogEntry *entry = l->data;
        GDateTime *entry_local = g_date_time_to_local(entry->timestamp);
        gint entry_year = g_date_time_get_year(entry_local);
        gint entry_day = g_date_time_get_day_of_year(entry_local);
        GtkWidget *row, *eventbox, *hbox;
        GtkWidget *summary, *timestamp, *body = NULL, *app_icon = NULL;
        const gchar *app_name = entry->app_name != NULL ? entry->app_name : entry->app_id;
        gchar *timestamp_text;
        gchar *summary_text;
        gchar *body_text;
        gchar *tooltip_timestamp_text;
        gchar *tooltip_text;
        cairo_surface_t *icon;

        timestamp_text = notify_log_format_timestamp(entry->timestamp, dt_format, custom_dt_format);
        summary_text = notify_log_format_summary(entry->summary);
        body_text = notify_log_format_body(entry->body);
        icon = notify_icon_for_entry(entry, icon_size, scale_factor, &emblem_color);
        tooltip_timestamp_text = notify_log_format_timestamp(entry->timestamp, XFCE_DATE_TIME_FORMAT_LOCALE, NULL);
        tooltip_text = notify_log_format_tooltip(app_name, tooltip_timestamp_text, body_text);

        if (!viewer->yesterday_header_added && (today_year != entry_year || today_day != entry_day)) {
            GtkWidget *header_row;
            GtkWidget *header;

            header_row = gtk_list_box_row_new();
            gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(header_row), FALSE);
            gtk_list_box_insert(GTK_LIST_BOX(viewer->listbox), header_row, -1);

            header = gtk_label_new (_("Yesterday and before"));
            gtk_widget_set_sensitive (header, FALSE);
            gtk_widget_set_margin_top (header, 3);
            gtk_widget_set_margin_bottom (header, 3);
            gtk_container_add(GTK_CONTAINER(header_row), header);

            viewer->yesterday_header_added = TRUE;;
        }

        summary = g_object_new(GTK_TYPE_LABEL,
                               "use-markup", TRUE,
                               "label", summary_text,
                               "ellipsize", PANGO_ELLIPSIZE_END,
                               "xalign", 0.0,
                               NULL);
        timestamp = g_object_new(GTK_TYPE_LABEL,
                                 "label", timestamp_text,
                                 "xalign", 1.0,
                                 NULL);
        if (body_text != NULL) {
            body = g_object_new(GTK_TYPE_LABEL,
                                "use-markup", TRUE,
                                "label", body_text,
                                "xalign", 0.0,
                                "ellipsize", PANGO_ELLIPSIZE_END,
                                NULL);
        }

        app_icon = gtk_image_new_from_surface(icon);
        gtk_widget_set_margin_start(app_icon, 3);

        row = gtk_list_box_row_new();
        g_object_set_data_full(G_OBJECT(row), LOG_ENTRY_KEY, xfce_notify_log_entry_ref(entry), (GDestroyNotify)xfce_notify_log_entry_unref);
        g_object_set_data(G_OBJECT(row), LOG_IMAGE_KEY, app_icon);

        eventbox = gtk_event_box_new();
        gtk_widget_add_events(eventbox, GDK_BUTTON_PRESS_MASK);
        gtk_container_add(GTK_CONTAINER(row), eventbox);
        g_signal_connect(eventbox, "button-press-event",
                         G_CALLBACK(xfce_notify_log_viewer_listbox_row_button_press), viewer);

        hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_tooltip_markup(hbox, tooltip_text);
        gtk_container_add(GTK_CONTAINER(eventbox), hbox);

        gtk_box_pack_start(GTK_BOX(hbox), app_icon, FALSE, FALSE, 0);
        if (body == NULL) {
            /* Handle icon-only notifications */
            GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
            gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

            gtk_box_pack_start(GTK_BOX(vbox), timestamp, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(vbox), summary, FALSE, FALSE, 0);
        } else {
            GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
            GtkWidget *inner_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

            gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
            gtk_box_pack_start(GTK_BOX(vbox), inner_hbox, FALSE, FALSE, 0);

            gtk_box_pack_start(GTK_BOX(inner_hbox), summary, TRUE, TRUE, 0);
            gtk_box_pack_end(GTK_BOX(inner_hbox), timestamp, FALSE, FALSE, 0);

            gtk_box_pack_start(GTK_BOX(vbox), body, FALSE, FALSE, 0);
        }

        gtk_widget_show_all(row);
        gtk_list_box_insert(GTK_LIST_BOX(viewer->listbox), row, -1);

        if (l->next == NULL) {
            g_free(viewer->last_entry_id);
            viewer->last_entry_id = g_strdup(entry->id);
        }

        g_free(timestamp_text);
        g_free(summary_text);
        g_free(body_text);
        g_free(tooltip_timestamp_text);
        g_free(tooltip_text);
        cairo_surface_destroy(icon);
        g_date_time_unref(entry_local);
    }

    g_date_time_unref(today);
    g_free(custom_dt_format);
}

static void
xfce_notify_log_viewer_populate(XfceNotifyLogViewer *viewer) {
    GList *children;

    children = gtk_container_get_children(GTK_CONTAINER(viewer->listbox));
    for (GList *l = children; l != NULL; l = l->next) {
        gtk_container_remove(GTK_CONTAINER(viewer->listbox), GTK_WIDGET(l->data));
    }
    g_list_free(children);

    viewer->yesterday_header_added = FALSE;
    g_free(viewer->last_entry_id);
    viewer->last_entry_id = NULL;
    viewer->loading_row = NULL;

    if (viewer->log != NULL) {
        GList *entries = xfce_notify_log_read(viewer->log, NULL, LOG_DISPLAY_LIMIT);
        xfce_notify_log_viewer_add_entries(viewer, entries);
        g_list_free_full(entries, (GDestroyNotify)xfce_notify_log_entry_unref);
    }

    gtk_widget_set_sensitive(GTK_WIDGET(viewer->clear_log_button),
                             gtk_list_box_get_row_at_index(GTK_LIST_BOX(viewer->listbox), 0) != NULL);
    gtk_widget_set_sensitive(GTK_WIDGET(viewer->mark_read_button),
                             viewer->log != NULL && xfce_notify_log_has_unread_messages(viewer->log));
}

static void
xfce_notify_log_viewer_refresh(XfceNotifyLogViewer *viewer) {
    if (viewer->log == NULL) {
        viewer->log = xfce_notify_log_open(NULL);
        if (viewer->log != NULL) {
            g_signal_connect_swapped(viewer->log, "changed",
                                     G_CALLBACK(xfce_notify_log_viewer_log_changed), viewer);
        }
    }

    if (viewer->log != NULL) {
        xfce_notify_log_viewer_populate(viewer);
    }
}

static void
xfce_notify_log_viewer_clear(XfceNotifyLogViewer *viewer) {
    GtkWidget *dialog;
    gint result;

    dialog = xfce_notify_clear_log_dialog(viewer->log);
    result = gtk_dialog_run(GTK_DIALOG(dialog));

    if (result == GTK_RESPONSE_OK) {
        /* Clear the listbox widget too in case the log is cleared */
        GList *children = gtk_container_get_children(GTK_CONTAINER(viewer->listbox));
        for (GList *l = children; l != NULL; l = l->next) {
            gtk_container_remove(GTK_CONTAINER(viewer->listbox), GTK_WIDGET(l->data));
        }
        g_list_free(children);
    }

    gtk_widget_destroy(dialog);
}

static void
xfce_notify_log_viewer_mark_read(XfceNotifyLogViewer *viewer) {
    xfce_notify_log_mark_all_read(viewer->log);
}

static void
xfce_notify_log_viewer_log_changed(XfceNotifyLogViewer *viewer) {
    // TODO: intelligently re-populate log
    gtk_widget_set_sensitive(GTK_WIDGET(viewer->mark_read_button), xfce_notify_log_count_unread_messages(viewer->log) > 0);
}

static gboolean
scroll_to_bottom_idled(GtkAdjustment *adj) {
    gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));
    g_object_unref(adj);
    return FALSE;
}

static gboolean
load_items_idled(XfceNotifyLogViewer *viewer) {
    GList *entries;

    viewer->load_items_id = 0;

    entries = xfce_notify_log_read(viewer->log, viewer->last_entry_id, LOG_DISPLAY_LIMIT);

    if (G_LIKELY(viewer->loading_row != NULL)) {
        gtk_widget_destroy(viewer->loading_row);
        viewer->loading_row = NULL;
    }

    xfce_notify_log_viewer_add_entries(viewer, entries);
    g_list_free_full(entries, (GDestroyNotify)xfce_notify_log_entry_unref);

    return FALSE;
}

static void
xfce_notify_log_viewer_scroll_edge_reached(XfceNotifyLogViewer *viewer, GtkPositionType pos) {
    if (pos == GTK_POS_BOTTOM && viewer->loading_row == NULL && viewer->log != NULL) {
        GtkWidget *row;
        GtkWidget *label;
        gchar *label_text;

        row = gtk_list_box_row_new();
        gtk_widget_show(row);
        gtk_list_box_insert(GTK_LIST_BOX(viewer->listbox), row, -1);

        label_text = g_strdup_printf("<big><i>%s</i></big>", _("Loading more log entries..."));
        label = xfce_notify_create_placeholder_label(label_text);
        gtk_widget_show(label);
        gtk_container_add(GTK_CONTAINER(row), label);

        viewer->loading_row = row;

        g_idle_add((GSourceFunc)scroll_to_bottom_idled,
                   g_object_ref(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(viewer->scroller))));
        viewer->load_items_id = g_idle_add((GSourceFunc)load_items_idled, viewer);

        g_free(label_text);
    }
}

GtkWidget *
xfce_notify_log_viewer_new(XfconfChannel *channel, XfceNotifyLog *log) {
    return g_object_new(XFCE_TYPE_NOTIFY_LOG_VIEWER,
                        "orientation", GTK_ORIENTATION_VERTICAL,
                        "channel", channel,
                        "log", log,
                        NULL);
}
