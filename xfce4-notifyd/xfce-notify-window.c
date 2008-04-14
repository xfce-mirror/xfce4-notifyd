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

#include <math.h>

#include <libxfcegui4/libxfcegui4.h>

#include "xfce-notify-window.h"

#define DEFAULT_EXPIRE_TIMEOUT    10000
#define DEFAULT_INITIAL_OPACITY       0.9
#define TRANS_CHANGE_TIMEOUT        150
#define DEFAULT_RADIUS               10.0
#define DEFAULT_BORDER_WIDTH          2.0
#define BORDER                        8

struct _XfceNotifyWindow
{
    GtkWindow parent;

    guint expire_timeout;

    gboolean mouse_hover;
    cairo_path_t *bg_path;

    gboolean fade_transparent;
    gdouble initial_opacity;

    GtkWidget *icon;
    GtkWidget *summary;
    GtkWidget *body;
    GtkWidget *button_box;
    
    guint64 expire_start_timestamp;
    guint expire_id;
    guint trans_id;
    guint op_change_steps;
    gdouble op_change_delta;
};

typedef struct
{
    GtkWindowClass parent;

    /*< signals >*/
    void (*action_invoked)(XfceNotifyWindow *window,
                           const gchar *action_id);
    void (*expired)(XfceNotifyWindow *window);
} XfceNotifyWindowClass;

enum
{
    SIG_CLICKED = 0,
    SIG_ACTION_INVOKED,
    SIG_EXPIRED,
    N_SIGS,
};

static void xfce_notify_window_class_init(XfceNotifyWindowClass *klass);
static void xfce_notify_window_init(XfceNotifyWindow *window);

static void xfce_notify_window_finalize(GObject *object);

static void xfce_notify_window_realize(GtkWidget *widget);
static void xfce_notify_window_unrealize(GtkWidget *widget);
static gboolean xfce_notify_window_expose(GtkWidget *widget,
                                          GdkEventExpose *evt);
static gboolean xfce_notify_window_enter_leave(GtkWidget *widget,
                                               GdkEventCrossing *evt);
static gboolean xfce_notify_window_button_release(GtkWidget *widget,
                                                  GdkEventButton *evt);

static gboolean xfce_notify_window_expire_timeout(gpointer data);
static gboolean xfce_notify_window_trans_timeout(gpointer data);

static void xfce_notify_window_button_clicked(GtkWidget *widget,
                                              gpointer user_data);

static void xfce_notify_window_setup_fade(XfceNotifyWindow *window);

static guint signals[N_SIGS] = { 0, };


G_DEFINE_TYPE(XfceNotifyWindow, xfce_notify_window, GTK_TYPE_WINDOW)


static void
xfce_notify_window_class_init(XfceNotifyWindowClass *klass)
{
    GObjectClass *gobject_class = (GObjectClass *)klass;
    GtkWidgetClass *widget_class = (GtkWidgetClass *)klass;

    gobject_class->finalize = xfce_notify_window_finalize;

    widget_class->realize = xfce_notify_window_realize;
    widget_class->unrealize = xfce_notify_window_unrealize;
    widget_class->expose_event = xfce_notify_window_expose;
    widget_class->enter_notify_event = xfce_notify_window_enter_leave;
    widget_class->leave_notify_event = xfce_notify_window_enter_leave;
    widget_class->button_release_event = xfce_notify_window_button_release;

    signals[SIG_ACTION_INVOKED] = g_signal_new("action-invoked",
                                               XFCE_TYPE_NOTIFY_WINDOW,
                                               G_SIGNAL_RUN_LAST,
                                               G_STRUCT_OFFSET(XfceNotifyWindowClass,
                                                               action_invoked),
                                               NULL, NULL,
                                               g_cclosure_marshal_VOID__STRING,
                                               G_TYPE_NONE,
                                               1, G_TYPE_STRING);

    signals[SIG_EXPIRED] = g_signal_new("expired",
                                        XFCE_TYPE_NOTIFY_WINDOW,
                                        G_SIGNAL_RUN_LAST,
                                        G_STRUCT_OFFSET(XfceNotifyWindowClass,
                                                        expired),
                                        NULL, NULL,
                                        g_cclosure_marshal_VOID__VOID,
                                        G_TYPE_NONE, 0);
    
    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_boxed("border-color",
                                                               "border color",
                                                               "the color of the prelight border",
                                                               GDK_TYPE_COLOR,
                                                               G_PARAM_READABLE));

    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_double("border-radius",
                                                                "border radius",
                                                                "the radius of the window border's curved corners",
                                                                0.0, 30.0,
                                                                DEFAULT_RADIUS,
                                                                G_PARAM_READABLE));
    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_double("border-width",
                                                                "border width",
                                                                "the width of the notification's border",
                                                                0.0, 8.0,
                                                                DEFAULT_BORDER_WIDTH,
                                                                G_PARAM_READABLE));
}

static void
xfce_notify_window_init(XfceNotifyWindow *window)
{
    GdkScreen *screen;
    GtkWidget *topvbox, *vbox, *hbox;
    
    GTK_WINDOW(window)->type = GTK_WINDOW_TOPLEVEL;
    window->expire_timeout = DEFAULT_EXPIRE_TIMEOUT;
    window->initial_opacity = DEFAULT_INITIAL_OPACITY;
    window->fade_transparent = TRUE;

    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_window_stick(GTK_WINDOW(window));
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_accept_focus(GTK_WINDOW(window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(window), BORDER);
    gtk_widget_set_app_paintable(GTK_WIDGET(window), TRUE);
    gtk_widget_add_events(GTK_WIDGET(window),
                          GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    
    screen = gtk_widget_get_screen(GTK_WIDGET(window));
    if(gdk_screen_is_composited(screen)) {
        GdkColormap *cmap = gdk_screen_get_rgba_colormap(screen);
        if(cmap)
            gtk_widget_set_colormap(GTK_WIDGET(window), cmap);
    } else {
        /* FIXME: do something useful */
    }

    topvbox = gtk_vbox_new(FALSE, BORDER);
    gtk_widget_show(topvbox);
    gtk_container_add(GTK_CONTAINER(window), topvbox);

    hbox = gtk_hbox_new(FALSE, BORDER);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(topvbox), hbox, TRUE, TRUE, 0);

    window->icon = gtk_image_new();
    gtk_box_pack_start(GTK_BOX(hbox), window->icon, FALSE, FALSE, 0);

    vbox = gtk_vbox_new(FALSE, BORDER);
    gtk_widget_show(vbox);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    window->summary = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(window->summary), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), window->summary, FALSE, FALSE, 0);

    window->body = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(window->body), TRUE);
    gtk_misc_set_alignment(GTK_MISC(window->body), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), window->body, TRUE, TRUE, 0);

    window->button_box = gtk_hbutton_box_new();
    gtk_button_box_set_spacing(GTK_BUTTON_BOX(window->button_box),
                               BORDER / 2);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(window->button_box),
                              GTK_BUTTONBOX_END);
    gtk_box_pack_start(GTK_BOX(topvbox), window->button_box, FALSE, FALSE, 0);
}

static void
xfce_notify_window_finalize(GObject *object)
{
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(object);

    if(window->bg_path)
        cairo_path_destroy(window->bg_path);

    G_OBJECT_CLASS(xfce_notify_window_parent_class)->finalize(object);
}

static void
xfce_notify_window_realize(GtkWidget *widget)
{
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(widget);

    GTK_WIDGET_CLASS(xfce_notify_window_parent_class)->realize(widget);

    if(window->expire_timeout) {
        GTimeVal ct;
        g_get_current_time(&ct);
        window->expire_start_timestamp = ct.tv_sec * 1000 + ct.tv_usec / 1000;
        window->expire_id = g_timeout_add(window->expire_timeout,
                                          xfce_notify_window_expire_timeout,
                                          window);
    }

    if(window->fade_transparent && window->expire_timeout)
        xfce_notify_window_setup_fade(window);
    else
        gtk_window_set_opacity(GTK_WINDOW(window), window->initial_opacity);
}

static void
xfce_notify_window_unrealize(GtkWidget *widget)
{
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(widget);

    if(window->trans_id) {
        g_source_remove(window->trans_id);
        window->trans_id = 0;
    }

    if(window->expire_id) {
        g_source_remove(window->expire_id);
        window->expire_id = 0;
    }

    GTK_WIDGET_CLASS(xfce_notify_window_parent_class)->unrealize(widget);
}

static inline cairo_path_t *
xfce_notify_window_ensure_bg_path(XfceNotifyWindow *window,
                                  cairo_t *cr)
{
    GtkWidget *widget = GTK_WIDGET(window);
    gdouble radius = DEFAULT_RADIUS;

    if(G_LIKELY(window->bg_path))
        return window->bg_path;

    gtk_widget_style_get(widget,
                         "border-radius", &radius,
                         NULL);

    if(radius < 0.1) {
        cairo_rectangle(cr, 0, 0, widget->allocation.width,
                        widget->allocation.height);
    } else {
        cairo_move_to(cr, 0, radius);
        cairo_arc(cr, radius, radius, radius, M_PI, 3.0*M_PI/2.0);
        cairo_line_to(cr, widget->allocation.width - radius, 0);
        cairo_arc(cr, widget->allocation.width - radius, radius, radius,
                  3.0*M_PI/2.0, 0.0);
        cairo_line_to(cr, widget->allocation.width,
                      widget->allocation.height - radius);
        cairo_arc(cr, widget->allocation.width - radius,
                  widget->allocation.height - radius, radius,
                  0.0, M_PI/2.0);
        cairo_line_to(cr, radius, widget->allocation.height);
        cairo_arc(cr, radius, widget->allocation.height - radius, radius,
                  M_PI/2.0, M_PI);
        cairo_close_path(cr);
    }

    window->bg_path = cairo_copy_path(cr);
    cairo_new_path(cr);

    return window->bg_path;
}

static gboolean
xfce_notify_window_expose(GtkWidget *widget,
                          GdkEventExpose *evt)
{
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(widget);
    GtkStyle *style = gtk_widget_get_style(widget);
    //GdkColor *c;
    cairo_t *cr;
    GList *children, *l;
    cairo_path_t *bg_path;

    if(evt->count != 0)
        return FALSE;

    cr = gdk_cairo_create(widget->window);

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    gdk_cairo_region(cr, evt->region);
    /* FIXME: would be nice to paint so it doesn't look ugly without a
     * compositor */
    //c = &style->bg[GTK_STATE_NORMAL];
    //cairo_set_source_rgba(cr, c->red/65535., c->green/65535.,
    //                      c->blue/65535., 0.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.0);
    cairo_fill_preserve(cr);
    cairo_clip(cr);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    gdk_cairo_set_source_color(cr, &style->bg[GTK_STATE_NORMAL]);
    bg_path = xfce_notify_window_ensure_bg_path(window, cr);
    cairo_append_path(cr, bg_path);
    cairo_fill_preserve(cr);

    if(window->mouse_hover) {
        GdkColor *border_color = NULL;
        gdouble border_width = DEFAULT_BORDER_WIDTH;

        gtk_widget_style_get(widget,
                             "border-color", &border_color,
                             "border-width", &border_width,
                             NULL);

        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        if(border_color)
            gdk_cairo_set_source_color(cr, border_color);
        else
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_line_width(cr, border_width);
        cairo_stroke(cr);
    }

    cairo_destroy(cr);

    children = gtk_container_get_children(GTK_CONTAINER(widget));
    for(l = children; l; l = l->next)
        gtk_container_propagate_expose(GTK_CONTAINER(widget), l->data, evt);
    g_list_free(children);

    return TRUE;
}

static gboolean
xfce_notify_window_enter_leave(GtkWidget *widget,
                               GdkEventCrossing *evt)
{
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(widget);

    if(evt->type == GDK_ENTER_NOTIFY) {
        if(window->expire_timeout) {
            if(window->expire_id) {
                g_source_remove(window->expire_id);
                window->expire_id = 0;
            }
            if(window->trans_id) {
                g_source_remove(window->trans_id);
                window->trans_id = 0;
            }
        }
        gtk_window_set_opacity(GTK_WINDOW(widget), 1.0);
        window->mouse_hover = TRUE;
        gtk_widget_queue_draw(widget);
    } else if(evt->type == GDK_LEAVE_NOTIFY
              && evt->detail != GDK_NOTIFY_INFERIOR)
    {
        if(window->expire_timeout) {
            GTimeVal ct;
            g_get_current_time(&ct);
            window->expire_start_timestamp = ct.tv_sec * 1000 + ct.tv_usec / 1000;
            window->expire_id = g_timeout_add(window->expire_timeout,
                                              xfce_notify_window_expire_timeout,
                                              window);
        }
        xfce_notify_window_setup_fade(window);
        window->mouse_hover = FALSE;
        gtk_widget_queue_draw(widget);
    }

    return FALSE;
}

static gboolean
xfce_notify_window_button_release(GtkWidget *widget,
                                        GdkEventButton *evt)
{
    g_signal_emit(G_OBJECT(widget), signals[SIG_ACTION_INVOKED], 0,
                  "default");
    return FALSE;
}



static gboolean
xfce_notify_window_expire_timeout(gpointer data)
{
    XfceNotifyWindow *window = data;
    window->expire_id = 0;
    g_signal_emit(G_OBJECT(window), signals[SIG_EXPIRED], 0);
    return FALSE;
}

static gboolean
xfce_notify_window_trans_timeout(gpointer data)
{
    XfceNotifyWindow *window = data;
    gdouble op = gtk_window_get_opacity(GTK_WINDOW(window));

    op -= window->op_change_delta;
    if(op < 0.0)
        op = 0.0;

    gtk_window_set_opacity(GTK_WINDOW(window), op);

    if(op <= 0.0001) {
        window->trans_id = 0;
        return FALSE;
    }

    return TRUE;
}

static void
xfce_notify_window_button_clicked(GtkWidget *widget,
                                  gpointer user_data)
{
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(user_data);
    gchar *action_id;

    action_id = g_object_get_data(G_OBJECT(widget), "--action-id");
    if(!action_id) {
        g_warning("No action id specified");
        return;
    }

    g_signal_emit(G_OBJECT(window), signals[SIG_ACTION_INVOKED], 0,
                  action_id);
}

/* FIXME: this doesn't pass valid markup properly if it's nested inside
 * other valid markup.  still generates a valid final string tho */
static gchar *
validate_markup(const gchar *str)
{
    GString *gstr;
    gchar *p;

    gstr = g_string_sized_new(strlen(str));
    p = (gchar *)str;

    while(*p) {
        if(*p == '<') {
            if((*(p+1) == 'b' || *(p+1) == 'i' || *(p+1) == 'u')
               && *(p+2) == '>')
            {
                gchar *q, cmp[5] = "</?>";
                cmp[2] = *(p+1);

                q = strstr(p, cmp);
                if(!q) {
                    g_warning("Bad markup in <%c>: %s", *(p+1), str);
                    g_string_free(gstr, TRUE);
                    return NULL;
                }

                g_string_append_c(gstr, *p);
                g_string_append_c(gstr, *(p+1));
                g_string_append_c(gstr, *(p+2));
                p += 3;

                while(p < q) {
                    if(*p == '<')
                        g_string_append(gstr, "&lt;");
                    else if(*p == '>')
                        g_string_append(gstr, "&gt;");
                    else if(*p == '&')
                        g_string_append(gstr, "&amp;");
                    else
                        g_string_append_c(gstr, *p);
                    p++;
                }
                g_string_append(gstr, cmp);
                p += 4;
            } else if(!strncmp(p + 1, "img ", 4)) {
                gchar *q, *r = NULL, *s;

                q = strstr(p, "alt=\"");
                if(q)
                    r = strchr(q + 5, '"');
                if(q && !r) {
                    g_warning("Bad markup in <img>: %s", str);
                    g_string_free(gstr, TRUE);
                    return NULL;
                }
                s = strchr(r ? r : p, '>');

                if(s && (!q || s < q))
                    p = r + 1;
                else if(!s) {
                    g_string_append(gstr, "&lt;");
                    p++;
                } else if(s && q && s > q) {
                    g_string_append_c(gstr, '[');
                    g_string_append(gstr, _("image: "));
                    p = q + 5;
                    while(p < r) {
                        if(*p == '<')
                            g_string_append(gstr, "&lt;");
                        else if(*p == '>')
                            g_string_append(gstr, "&gt;");
                        else if(*p == '&')
                            g_string_append(gstr, "&amp;");
                        else
                            g_string_append_c(gstr, *p);
                        p++;
                    }
                    g_string_append_c(gstr, ']');
                    p = s + 1;
                } else
                    g_warning("Unhandled <img> case");
            } else if(!strncmp(p + 1, "a ", 2)) {
                gchar *q = strchr(p+2, '>');
                if(!q) {
                    g_warning("Bad markup in <a>: %s", str);
                    g_string_free(gstr, TRUE);
                    return NULL;
                }

                p = q + 1;

                q = strstr(p, "</a>");
                if(!q) {
                    g_warning("Bad markup finding </a>: %s", str);
                    g_string_free(gstr, TRUE);
                    return NULL;
                }

                while(p < q) {
                    if(*p == '<')
                        g_string_append(gstr, "&lt;");
                    else if(*p == '>')
                        g_string_append(gstr, "&gt;");
                    else if(*p == '&')
                        g_string_append(gstr, "&amp;");
                    else
                        g_string_append_c(gstr, *p);
                    p++;
                }
                p += 4;
            } else {
                g_string_append(gstr, "&lt;");
                p++;
            }
        } else if(*p == '>') {
            g_string_append(gstr, "&gt;");
            p++;
        } else if(*p == '&') {
            g_string_append(gstr, "&amp;");
            p++;
        } else {
            g_string_append_c(gstr, *p);
            p++;
        }
    }

    p = gstr->str;
    g_string_free(gstr, FALSE);

    return p;
}

static void
xfce_notify_window_setup_fade(XfceNotifyWindow *window)
{
    GTimeVal ct;
    guint64 t;
    guint time_left, steps_left;
    gdouble op;

    if(!window->expire_timeout || !window->fade_transparent) {
        gtk_window_set_opacity(GTK_WINDOW(window), window->initial_opacity);
        return;
    }

    if(window->trans_id) {
        g_source_remove(window->trans_id);
        window->trans_id = 0;
    }

    g_get_current_time(&ct);

    t = ct.tv_sec * 1000 + ct.tv_usec / 1000;
    time_left = window->expire_timeout - (t - window->expire_start_timestamp);
    steps_left = time_left / TRANS_CHANGE_TIMEOUT;
    op = window->initial_opacity - (window->op_change_steps - steps_left) * window->op_change_delta;
    gtk_window_set_opacity(GTK_WINDOW(window), op);

    window->trans_id = g_timeout_add(TRANS_CHANGE_TIMEOUT,
                                     xfce_notify_window_trans_timeout,
                                     window);
}



GtkWidget *
xfce_notify_window_new()
{
    return xfce_notify_window_new_with_actions(NULL, NULL, NULL, -1, NULL);
}

GtkWidget *
xfce_notify_window_new_full(const gchar *summary,
                            const gchar *body,
                            const gchar *icon_name,
                            gint expire_timeout)
{
    return xfce_notify_window_new_with_actions(summary, body,
                                               icon_name,
                                               expire_timeout,
                                               NULL);
}

GtkWidget *
xfce_notify_window_new_with_actions(const gchar *summary,
                                    const gchar *body,
                                    const gchar *icon_name,
                                    gint expire_timeout,
                                    const gchar **actions)
{
    XfceNotifyWindow *window;

    window = g_object_new(XFCE_TYPE_NOTIFY_WINDOW, NULL);
    
    xfce_notify_window_set_summary(window, summary);
    xfce_notify_window_set_body(window, body);
    xfce_notify_window_set_icon_name(window, icon_name);
    xfce_notify_window_set_expire_timeout(window, expire_timeout);
    xfce_notify_window_set_actions(window, actions);
    
    return GTK_WIDGET(window);
}

void
xfce_notify_window_set_summary(XfceNotifyWindow *window,
                               const gchar *summary)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));
    
    gtk_label_set_text(GTK_LABEL(window->summary), summary);
    if(summary && *summary)
        gtk_widget_show(window->summary);
    else
        gtk_widget_hide(window->summary);
}

void
xfce_notify_window_set_body(XfceNotifyWindow *window,
                            const gchar *body)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));
    
    if(body && *body) {
        gchar *markup = validate_markup(body);
        gtk_label_set_markup(GTK_LABEL(window->body), markup);
        gtk_widget_show(window->body);
        g_free(markup);
    } else {
        gtk_label_set_markup(GTK_LABEL(window->body), "");
        gtk_widget_hide(window->body);
    }
}

void
xfce_notify_window_set_icon_name(XfceNotifyWindow *window,
                                 const gchar *icon_name)
{
    gboolean icon_set = FALSE;
    
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    if(icon_name && *icon_name) {
        gint w, h;
        GdkPixbuf *pix;
        
        gtk_icon_size_lookup(GTK_ICON_SIZE_DIALOG, &w, &h);
        pix = xfce_themed_icon_load(icon_name, w);
        if(pix) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(window->icon), pix);
            gtk_widget_show(window->icon);
            g_object_unref(G_OBJECT(pix));
            icon_set = TRUE;
        }
    }
    
    if(!icon_set) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(window->icon), NULL);
        gtk_widget_hide(window->icon);
    }
}

void
xfce_notify_window_set_icon_pixbuf(XfceNotifyWindow *window,
                                   GdkPixbuf *pixbuf)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window)
                     && (!pixbuf || GDK_IS_PIXBUF(pixbuf)));

    gtk_image_set_from_pixbuf(GTK_IMAGE(window->icon), pixbuf);

    if(pixbuf)
        gtk_widget_show(window->icon);
    else
        gtk_widget_hide(window->icon);
}

void
xfce_notify_window_set_expire_timeout(XfceNotifyWindow *window,
                                      gint expire_timeout)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    if(expire_timeout >= 0)
        window->expire_timeout = expire_timeout;
    else
        window->expire_timeout = DEFAULT_EXPIRE_TIMEOUT;

    window->op_change_steps = window->expire_timeout / TRANS_CHANGE_TIMEOUT;
    xfce_notify_window_set_opacity(window, window->initial_opacity);

    if(GTK_WIDGET_REALIZED(window)) {
        if(window->expire_id) {
            g_source_remove(window->expire_id);
            window->expire_id = 0;
        }

        if(window->expire_timeout) {
            GTimeVal ct;
            g_get_current_time(&ct);
            window->expire_start_timestamp = ct.tv_sec * 1000 + ct.tv_usec / 1000;
            window->expire_id = g_timeout_add(window->expire_timeout,
                                              xfce_notify_window_expire_timeout,
                                              window);
        }

        gtk_window_set_opacity(GTK_WINDOW(window), window->initial_opacity);
    }
}

void
xfce_notify_window_set_actions(XfceNotifyWindow *window,
                               const gchar **actions)
{
    gint i;

    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    if(!actions)
        return;

    for(i = 0; actions[i]; i += 2) {
        const gchar *cur_action_id = actions[i];
        const gchar *cur_button_text = actions[i+1];
        GtkWidget *btn, *lbl;
        gchar *cur_button_text_escaped, *btn_text;

        if(!cur_button_text)
            break;

        if(!i)
            gtk_widget_show(window->button_box);

        btn = gtk_button_new();
        g_object_set_data_full(G_OBJECT(btn), "--action-id",
                               g_strdup(cur_action_id),
                               (GDestroyNotify)g_free);
        gtk_widget_show(btn);
        gtk_container_add(GTK_CONTAINER(window->button_box), btn);
        g_signal_connect(G_OBJECT(btn), "clicked",
                         G_CALLBACK(xfce_notify_window_button_clicked),
                         window);

        cur_button_text_escaped = g_markup_escape_text(cur_button_text, -1);
        btn_text = g_strconcat("<span size='small'>",
                               cur_button_text_escaped,
                               "</span>", NULL);

        lbl = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(lbl), btn_text);
        gtk_label_set_use_markup(GTK_LABEL(lbl), TRUE);
        gtk_widget_show(lbl);
        gtk_container_add(GTK_CONTAINER(btn), lbl);

        g_free(cur_button_text_escaped);
        g_free(btn_text);
    }

}

void
xfce_notify_window_set_fade_transparent(XfceNotifyWindow *window,
                                        gboolean fade_transparent)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    if(fade_transparent == window->fade_transparent)
        return;

    window->fade_transparent = fade_transparent;

    if(!GTK_WIDGET_REALIZED(window) || window->mouse_hover)
        return;

    if(fade_transparent)
        xfce_notify_window_setup_fade(window);
    else {
        if(window->trans_id) {
            g_source_remove(window->trans_id);
            window->trans_id = 0;
        }
        gtk_window_set_opacity(GTK_WINDOW(window), window->initial_opacity);
    }
}

gboolean
xfce_notify_window_get_fade_transparent(XfceNotifyWindow *window)
{
    g_return_val_if_fail(XFCE_IS_NOTIFY_WINDOW(window), FALSE);
    return window->fade_transparent;
}

void
xfce_notify_window_set_opacity(XfceNotifyWindow *window,
                               gdouble opacity)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    if(opacity > 1.0)
        opacity = 1.0;
    else if(opacity < 0.0)
        opacity = 0.0;

    window->initial_opacity = opacity;
    window->op_change_delta = opacity / window->op_change_steps;

    /* FIXME: recalc current opacity if realized */
}

gdouble
xfce_notify_window_get_opacity(XfceNotifyWindow *window)
{
    g_return_val_if_fail(XFCE_IS_NOTIFY_WINDOW(window), 0.0);
    return window->initial_opacity;
}
