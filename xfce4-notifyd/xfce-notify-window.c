/*
 *  xfce4-notifyd
 *
 *  Copyright (c) 2008-2009 Brian Tarricone <bjt23@cornell.edu>
 *  Copyright (c) 2009 Jérôme Guelfucci <jeromeg@xfce.org>
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

#include <libxfce4ui/libxfce4ui.h>

#include "xfce-notify-window.h"
#include "xfce-notify-enum-types.h"

#define DEFAULT_EXPIRE_TIMEOUT 10000
#define DEFAULT_NORMAL_OPACITY 0.85
#define FADE_TIME              800
#define FADE_CHANGE_TIMEOUT    50
#define DEFAULT_RADIUS         10.0
#define DEFAULT_BORDER_WIDTH   2.0
#define DEFAULT_PADDING        14.0
#define BORDER                 6

struct _XfceNotifyWindow
{
    GtkWindow parent;

    GdkRectangle geometry;
    gint last_monitor;
    gint last_screen;

    guint expire_timeout;

    gboolean mouse_hover;
    cairo_path_t *bg_path;

    gdouble normal_opacity;

    guint32 icon_only:1,
            has_summary_text:1,
            has_body_text:1,
            has_actions;

    GtkWidget *icon_box;
    GtkWidget *icon;
    GtkWidget *content_box;
    GtkWidget *gauge;
    GtkWidget *summary;
    GtkWidget *body;
    GtkWidget *button_box;

    guint64 expire_start_timestamp;
    guint expire_id;
    guint fade_id;
    guint op_change_steps;
    gdouble op_change_delta;
};

typedef struct
{
    GtkWindowClass parent;

    /*< signals >*/
    void (*closed)(XfceNotifyWindow *window,
                   XfceNotifyCloseReason reason);
    void (*action_invoked)(XfceNotifyWindow *window,
                           const gchar *action_id);
} XfceNotifyWindowClass;

enum
{
    SIG_CLOSED = 0,
    SIG_ACTION_INVOKED,
    N_SIGS,
};

static void xfce_notify_window_finalize(GObject *object);

static void xfce_notify_window_realize(GtkWidget *widget);
static void xfce_notify_window_unrealize(GtkWidget *widget);
static gboolean xfce_notify_window_expose(GtkWidget *widget,
                                          GdkEventExpose *evt);
static gboolean xfce_notify_window_enter_leave(GtkWidget *widget,
                                               GdkEventCrossing *evt);
static gboolean xfce_notify_window_button_release(GtkWidget *widget,
                                                  GdkEventButton *evt);
static gboolean xfce_notify_window_configure_event(GtkWidget *widget,
                                                   GdkEventConfigure *evt);
static gboolean xfce_notify_window_expire_timeout(gpointer data);
static gboolean xfce_notify_window_fade_timeout(gpointer data);

static void xfce_notify_window_button_clicked(GtkWidget *widget,
                                              gpointer user_data);

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
    widget_class->configure_event = xfce_notify_window_configure_event;

    signals[SIG_CLOSED] = g_signal_new("closed",
                                       XFCE_TYPE_NOTIFY_WINDOW,
                                       G_SIGNAL_RUN_LAST,
                                       G_STRUCT_OFFSET(XfceNotifyWindowClass,
                                                       closed),
                                       NULL, NULL,
                                       g_cclosure_marshal_VOID__ENUM,
                                       G_TYPE_NONE, 1,
                                       XFCE_TYPE_NOTIFY_CLOSE_REASON);

    signals[SIG_ACTION_INVOKED] = g_signal_new("action-invoked",
                                               XFCE_TYPE_NOTIFY_WINDOW,
                                               G_SIGNAL_RUN_LAST,
                                               G_STRUCT_OFFSET(XfceNotifyWindowClass,
                                                               action_invoked),
                                               NULL, NULL,
                                               g_cclosure_marshal_VOID__STRING,
                                               G_TYPE_NONE,
                                               1, G_TYPE_STRING);

    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_boxed("border-color",
                                                               "border color",
                                                               "the color of the prelight border",
                                                               GDK_TYPE_COLOR,
                                                               G_PARAM_READABLE));

    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_boxed("border-color-hover",
                                                               "border color hover",
                                                               "the color of the border when hovering the notification",
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
    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_double("border-width-hover",
                                                                "border width hover",
                                                                "the width of the border when hovering the notification",
                                                                0.0, 8.0,
                                                                DEFAULT_BORDER_WIDTH,
                                                                G_PARAM_READABLE));
    gtk_widget_class_install_style_property(widget_class,
                                            g_param_spec_double("padding",
                                                                "padding",
                                                                "the padding of the text/icon to the notification's border",
                                                                0.0, 30.0,
                                                                DEFAULT_PADDING,
                                                                G_PARAM_READABLE));
}

static void
xfce_notify_window_init(XfceNotifyWindow *window)
{
    GdkScreen *screen;
    GtkWidget *tophbox, *align, *vbox;
    gdouble padding = DEFAULT_PADDING;

    window->expire_timeout = DEFAULT_EXPIRE_TIMEOUT;
    window->normal_opacity = DEFAULT_NORMAL_OPACITY;

    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_window_stick(GTK_WINDOW(window));
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_accept_focus(GTK_WINDOW(window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(window),
                             GDK_WINDOW_TYPE_HINT_NOTIFICATION);
    gtk_container_set_border_width(GTK_CONTAINER(window), 0);
    gtk_widget_set_app_paintable(GTK_WIDGET(window), TRUE);
    gtk_widget_add_events(GTK_WIDGET(window),
                          GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
                          | GDK_POINTER_MOTION_MASK);

    screen = gtk_widget_get_screen(GTK_WIDGET(window));
    if(gdk_screen_is_composited(screen)) {
        GdkColormap *cmap = gdk_screen_get_rgba_colormap(screen);
        if(cmap)
            gtk_widget_set_colormap(GTK_WIDGET(window), cmap);
    }

    gtk_widget_ensure_style(GTK_WIDGET(window));
    gtk_widget_style_get(GTK_WIDGET(window),
                         "padding", &padding,
                         NULL);

    tophbox = gtk_hbox_new(FALSE, BORDER);
    gtk_container_set_border_width(GTK_CONTAINER(tophbox), padding);
    gtk_widget_show(tophbox);
    gtk_container_add(GTK_CONTAINER(window), tophbox);

    window->icon_box = align = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_container_set_border_width(GTK_CONTAINER(align), 0);
    gtk_box_pack_start(GTK_BOX(tophbox), align, FALSE, TRUE, 0);

    window->icon = gtk_image_new();
    gtk_widget_show(window->icon);
    gtk_container_add(GTK_CONTAINER(align), window->icon);

    window->content_box = vbox = gtk_vbox_new(FALSE, BORDER);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 0);
    gtk_widget_show(vbox);
    gtk_box_pack_start(GTK_BOX(tophbox), vbox, TRUE, TRUE, 0);

    window->summary = gtk_label_new(NULL);
    gtk_widget_set_name (window->summary, "summary");
    gtk_label_set_line_wrap(GTK_LABEL(window->summary), TRUE);
    gtk_misc_set_alignment(GTK_MISC(window->summary), 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), window->summary, FALSE, FALSE, 0);

    window->body = gtk_label_new(NULL);
    gtk_widget_set_name (window->body, "body");
    gtk_label_set_line_wrap(GTK_LABEL(window->body), TRUE);
    gtk_misc_set_alignment(GTK_MISC(window->body), 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), window->body, TRUE, TRUE, 0);

    window->button_box = gtk_hbutton_box_new();
    gtk_button_box_set_layout(GTK_BUTTON_BOX(window->button_box),
                              GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(window->button_box), BORDER / 2);
    gtk_box_set_homogeneous(GTK_BOX(window->button_box), FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), window->button_box, FALSE, FALSE, 0);
}

static void
xfce_notify_window_start_expiration(XfceNotifyWindow *window)
{
    if(window->expire_timeout) {
        GTimeVal ct;
        guint timeout;
        gboolean fade_transparent;

        g_get_current_time(&ct);

        fade_transparent =
            gdk_screen_is_composited(gtk_window_get_screen(GTK_WINDOW (window)));

        if(!fade_transparent)
            timeout = window->expire_timeout;
        else if(window->expire_timeout > FADE_TIME)
            timeout = window->expire_timeout - FADE_TIME;
        else
            timeout = FADE_TIME;

        window->expire_start_timestamp = ct.tv_sec * 1000 + ct.tv_usec / 1000;
        window->expire_id = g_timeout_add(timeout,
                                          xfce_notify_window_expire_timeout,
                                          window);
    }

    gtk_window_set_opacity(GTK_WINDOW(window), window->normal_opacity);
}

static void
xfce_notify_window_finalize(GObject *object)
{
    G_OBJECT_CLASS(xfce_notify_window_parent_class)->finalize(object);
}

static void
xfce_notify_window_realize(GtkWidget *widget)
{
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(widget);

    GTK_WIDGET_CLASS(xfce_notify_window_parent_class)->realize(widget);

    gdk_window_set_type_hint(widget->window,
                             GDK_WINDOW_TYPE_HINT_NOTIFICATION);
    xfce_notify_window_start_expiration(window);
}

static void
xfce_notify_window_unrealize(GtkWidget *widget)
{
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(widget);

    if(window->fade_id) {
        g_source_remove(window->fade_id);
        window->fade_id = 0;
    }

    if(window->expire_id) {
        g_source_remove(window->expire_id);
        window->expire_id = 0;
    }

    GTK_WIDGET_CLASS(xfce_notify_window_parent_class)->unrealize(widget);

    if(window->bg_path) {
        cairo_path_destroy(window->bg_path);
        window->bg_path = NULL;
    }
}

static inline GdkRegion *
xfce_gdk_region_from_cairo_flat_path(cairo_path_t *flat_path,
                                     GdkFillRule fill_rule)
{
    GdkRegion *region = NULL;
    gint i;
    cairo_path_data_t *data;
    GdkPoint *points;
    gint max_points = 10, n_points = 0;

    points = g_malloc(sizeof(GdkPoint) * max_points);

    for(i = 0;
        i < flat_path->num_data;
        i += flat_path->data[i].header.length)
    {
        if(max_points == n_points) {
            max_points += 10;
            points = g_realloc(points, sizeof(GdkPoint) * max_points);
        }

        data = &flat_path->data[i];
        switch(data->header.type) {
            case CAIRO_PATH_MOVE_TO:
                points[n_points].x = data[1].point.x;
                points[n_points].y = data[1].point.y;
                n_points++;
                break;

            case CAIRO_PATH_LINE_TO:
                points[n_points].x = data[1].point.x;
                points[n_points].y = data[1].point.y;
                n_points++;
                break;

            case CAIRO_PATH_CURVE_TO:
                g_warning("xfce_gdk_region_from_cairo_flat_path() called with non-flat path");
                goto out_error;

            case CAIRO_PATH_CLOSE_PATH:
                if(n_points < 2) {
                    g_warning("Tried to close path with < 2 points");
                    goto out_error;
                }
                points[n_points].x = points[0].x;
                points[n_points].y = points[0].y;
                n_points++;
                break;
        }
    }

    region = gdk_region_polygon(points, n_points, fill_rule);

out_error:
    g_free(points);

    return region;
}

static inline cairo_path_t *
xfce_notify_window_ensure_bg_path(XfceNotifyWindow *window,
                                  cairo_t *cr)
{
    GtkWidget *widget = GTK_WIDGET(window);
    gdouble radius = DEFAULT_RADIUS;
    gdouble border_width = DEFAULT_BORDER_WIDTH;

    /* this secifies the border_padding from the edges in order to make
     * sure the border completely fits into the drawing area */
    gdouble border_padding = 0.0;

    cairo_path_t *flat_path;
    GdkRegion *region;
    GdkFillRule fill_rule;
    GtkRequisition req;

    if(G_LIKELY(window->bg_path))
        return window->bg_path;

    gtk_widget_size_request(GTK_WIDGET(window), &req);

    if (!window->mouse_hover) {
        gtk_widget_style_get(widget,
                             "border-radius", &radius,
                             "border-width", &border_width,
                             NULL);
    } else {
        gtk_widget_style_get(widget,
                             "border-radius", &radius,
                             "border-width-hover", &border_width,
                             NULL);
    }

    border_padding = border_width / 2.0;

    if(radius < 0.1) {
        cairo_rectangle(cr, 0, 0, widget->allocation.width,
                        widget->allocation.height);
    } else {
        cairo_move_to(cr, border_padding, radius + border_padding);
        cairo_arc(cr, radius + border_padding,
                  radius + border_padding, radius,
                  M_PI, 3.0*M_PI/2.0);
        cairo_line_to(cr,
                      widget->allocation.width - radius - border_padding,
                      border_padding);
        cairo_arc(cr,
                  widget->allocation.width - radius - border_padding,
                  radius + border_padding, radius,
                  3.0*M_PI/2.0, 0.0);
        cairo_line_to(cr, widget->allocation.width - border_padding,
                      widget->allocation.height - radius - border_padding);
        cairo_arc(cr, widget->allocation.width - radius - border_padding,
                  widget->allocation.height - radius - border_padding,
                  radius, 0.0, M_PI/2.0);
        cairo_line_to(cr, radius + border_padding,
                      widget->allocation.height - border_padding);
        cairo_arc(cr, radius + border_padding,
                  widget->allocation.height - radius - border_padding,
                  radius, M_PI/2.0, M_PI);
        cairo_close_path(cr);
    }

    window->bg_path = cairo_copy_path(cr);

    flat_path = cairo_copy_path_flat(cr);
    fill_rule = (cairo_get_fill_rule(cr) == CAIRO_FILL_RULE_WINDING
                 ? GDK_WINDING_RULE : GDK_EVEN_ODD_RULE);
    region = xfce_gdk_region_from_cairo_flat_path(flat_path, fill_rule);
    cairo_path_destroy(flat_path);
    /* only set the window shape if the widget isn't composited; otherwise
     * the shape might further constrain the window, and because the
     * path flattening isn't an exact science, it looks ugly. */
    if(!gtk_widget_is_composited(widget))
        gdk_window_shape_combine_region(widget->window, region, 0, 0);
    /* however, of course always set the input shape; it doesn't matter
     * if this is a pixel or two off here and there */
    gdk_window_input_shape_combine_region(widget->window, region, 0, 0);
    gdk_region_destroy(region);

    cairo_new_path(cr);

    return window->bg_path;
}

static gboolean
xfce_notify_window_expose(GtkWidget *widget,
                          GdkEventExpose *evt)
{
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(widget);
    GtkStyle *style = gtk_widget_get_style(widget);
    cairo_t *cr;
    GList *children, *l;
    cairo_path_t *bg_path;
    GdkColor *border_color = NULL;
    gdouble border_width = DEFAULT_BORDER_WIDTH;

    if(evt->count != 0)
        return FALSE;

    cr = gdk_cairo_create(widget->window);
    bg_path = xfce_notify_window_ensure_bg_path(window, cr);

    /* the idea here is we only do the fancy semi-transparent shaped
     * painting if the widget is composited.  if not, we avoid artifacts
     * and optimise a bit by just painting the entire thing and relying
     * on the window shape mask to effectively "clip" drawing for us */
    if(gtk_widget_is_composited(widget)) {
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        gdk_cairo_region(cr, evt->region);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.0);
        cairo_fill_preserve(cr);
        cairo_clip(cr);

        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        gdk_cairo_set_source_color(cr, &style->bg[GTK_STATE_NORMAL]);
        cairo_append_path(cr, bg_path);
        cairo_fill_preserve(cr);
    } else {
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        gdk_cairo_set_source_color(cr, &style->bg[GTK_STATE_NORMAL]);
        cairo_fill(cr);

        cairo_append_path(cr, bg_path);
    }

    if(window->mouse_hover) {
        gtk_widget_style_get(widget,
                             "border-color-hover", &border_color,
                             "border-width-hover", &border_width,
                             NULL);
    } else {
        gtk_widget_style_get(widget,
                             "border-color", &border_color,
                             "border-width", &border_width,
                             NULL);
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    if(border_color)
        gdk_cairo_set_source_color(cr, border_color);
    else
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, border_width);

    cairo_stroke(cr);
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
            if(window->fade_id) {
                g_source_remove(window->fade_id);
                window->fade_id = 0;
            }
        }
        gtk_window_set_opacity(GTK_WINDOW(widget), 1.0);
        window->mouse_hover = TRUE;
        gtk_widget_queue_draw(widget);
    } else if(evt->type == GDK_LEAVE_NOTIFY
              && evt->detail != GDK_NOTIFY_INFERIOR)
    {
        xfce_notify_window_start_expiration(window);
        window->mouse_hover = FALSE;
        gtk_widget_queue_draw(widget);
    }

    return FALSE;
}

static gboolean
xfce_notify_window_button_release(GtkWidget *widget,
                                  GdkEventButton *evt)
{
    g_signal_emit(G_OBJECT(widget), signals[SIG_CLOSED], 0,
                  XFCE_NOTIFY_CLOSE_REASON_DISMISSED);

    return FALSE;
}

static gboolean
xfce_notify_window_configure_event(GtkWidget *widget,
                                   GdkEventConfigure *evt)
{
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(widget);
    gboolean ret;

    ret = GTK_WIDGET_CLASS(xfce_notify_window_parent_class)->configure_event(widget,
                                                                             evt);

    if(window->bg_path) {
        cairo_path_destroy(window->bg_path);
        window->bg_path = NULL;
    }

    gtk_widget_queue_draw(widget);

    return ret;
}



static gboolean
xfce_notify_window_expire_timeout(gpointer data)
{
    XfceNotifyWindow *window = data;
    gboolean          fade_transparent;

    window->expire_id = 0;

    fade_transparent =
        gdk_screen_is_composited(gtk_window_get_screen(GTK_WINDOW(window)));

    if(fade_transparent) {
        window->fade_id = g_timeout_add(FADE_CHANGE_TIMEOUT,
                                        xfce_notify_window_fade_timeout,
                                        window);
    } else {
        /* it might be 800ms early, but that's ok */
        g_signal_emit(G_OBJECT(window), signals[SIG_CLOSED], 0,
                      XFCE_NOTIFY_CLOSE_REASON_EXPIRED);
    }

    return FALSE;
}

static gboolean
xfce_notify_window_fade_timeout(gpointer data)
{
    XfceNotifyWindow *window = data;
    gdouble op = gtk_window_get_opacity(GTK_WINDOW(window));

    op -= window->op_change_delta;
    if(op < 0.0)
        op = 0.0;

    gtk_window_set_opacity(GTK_WINDOW(window), op);

    if(op <= 0.0001) {
        window->fade_id = 0;
        g_signal_emit(G_OBJECT(window), signals[SIG_CLOSED], 0,
                      XFCE_NOTIFY_CLOSE_REASON_EXPIRED);
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
    g_assert(action_id);

    g_signal_emit(G_OBJECT(window), signals[SIG_ACTION_INVOKED], 0,
                  action_id);
    g_signal_emit(G_OBJECT(window), signals[SIG_CLOSED], 0,
                  XFCE_NOTIFY_CLOSE_REASON_DISMISSED);
}

#define ELEM_B    GUINT_TO_POINTER(1)
#define ELEM_I    GUINT_TO_POINTER(2)
#define ELEM_U    GUINT_TO_POINTER(3)
#define ELEM_A    GUINT_TO_POINTER(4)

static inline const gchar *
elem_to_string(gconstpointer elem_p)
{
    gint elem = GPOINTER_TO_UINT(elem_p);
    switch(elem) {
        case GPOINTER_TO_UINT(ELEM_B):
            return "<b>";
        case GPOINTER_TO_UINT(ELEM_I):
            return "<i>";
        case GPOINTER_TO_UINT(ELEM_U):
            return "<u>";
        case GPOINTER_TO_UINT(ELEM_A):
            return "<a>";
        default:
            return "??";
    }
}

GtkWidget *
xfce_notify_window_new(void)
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

    window = g_object_new(XFCE_TYPE_NOTIFY_WINDOW,
                          "type", GTK_WINDOW_TOPLEVEL, NULL);

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
    if(summary && *summary) {
        gtk_widget_show(window->summary);
        window->has_summary_text = TRUE;
    } else {
        gtk_widget_hide(window->summary);
        window->has_summary_text = FALSE;
    }

    if(window->bg_path) {
        cairo_path_destroy(window->bg_path);
        window->bg_path = NULL;
        gtk_widget_queue_draw(GTK_WIDGET(window));
    }
}

void
xfce_notify_window_set_body(XfceNotifyWindow *window,
                            const gchar *body)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    if(body && *body) {
        gtk_label_set_markup(GTK_LABEL(window->body), body);

        gtk_widget_show(window->body);

        window->has_body_text = TRUE;
    } else {
        gtk_label_set_markup(GTK_LABEL(window->body), "");
        gtk_widget_hide(window->body);
        window->has_body_text = FALSE;
    }

    if(window->bg_path) {
        cairo_path_destroy(window->bg_path);
        window->bg_path = NULL;
        gtk_widget_queue_draw(GTK_WIDGET(window));
    }
}

void
xfce_notify_window_set_geometry(XfceNotifyWindow *window,
                                GdkRectangle rectangle)
{
    window->geometry = rectangle;
}

GdkRectangle *
xfce_notify_window_get_geometry (XfceNotifyWindow *window)
{
   return &window->geometry;
}

void
xfce_notify_window_set_last_monitor(XfceNotifyWindow *window,
                                    gint monitor)
{
    window->last_monitor = monitor;
}

gint
xfce_notify_window_get_last_monitor(XfceNotifyWindow *window)
{
   return window->last_monitor;
}

void
xfce_notify_window_set_last_screen(XfceNotifyWindow *window,
                                   gint screen)
{
    window->last_screen = screen;
}

gint
xfce_notify_window_get_last_screen(XfceNotifyWindow *window)
{
    return window->last_screen;
}

void
xfce_notify_window_set_icon_name(XfceNotifyWindow *window,
                                 const gchar *icon_name)
{
    gboolean icon_set = FALSE;
    gchar *filename;

    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    if(icon_name && *icon_name) {
        gint w, h;
        GdkPixbuf *pix;

        gtk_icon_size_lookup(GTK_ICON_SIZE_DIALOG, &w, &h);

        if(g_path_is_absolute(icon_name))
          pix = gdk_pixbuf_new_from_file_at_size(icon_name, w, h, NULL);
        else if(g_str_has_prefix (icon_name, "file://")) {
            filename = g_filename_from_uri(icon_name, NULL, NULL);
            if(filename)
              pix = gdk_pixbuf_new_from_file_at_size(filename, w, h, NULL);
            g_free(filename);
          }
        else
          pix = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
                                       icon_name,
                                       w,
                                       GTK_ICON_LOOKUP_FORCE_SIZE,
                                       NULL);

        if(pix) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(window->icon), pix);
            gtk_widget_show(window->icon_box);
            g_object_unref(G_OBJECT(pix));
            icon_set = TRUE;
        }
    }

    if(!icon_set) {
        gtk_image_clear(GTK_IMAGE(window->icon));
        gtk_widget_hide(window->icon_box);
    }

    if(window->bg_path) {
        cairo_path_destroy(window->bg_path);
        window->bg_path = NULL;
        gtk_widget_queue_draw(GTK_WIDGET(window));
    }
}

void
xfce_notify_window_set_icon_pixbuf(XfceNotifyWindow *window,
                                   GdkPixbuf *pixbuf)
{
    GdkPixbuf *p_free = NULL;

    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window)
                     && (!pixbuf || GDK_IS_PIXBUF(pixbuf)));

    if(pixbuf) {
        gint w, h, pw, ph;

        gtk_icon_size_lookup(GTK_ICON_SIZE_DIALOG, &w, &h);
        pw = gdk_pixbuf_get_width(pixbuf);
        ph = gdk_pixbuf_get_height(pixbuf);

        if(w > h)
            w = h;
        if(pw > w || ph > w) {
            gint nw, nh;

            if(pw > ph) {
                nw = w;
                nh = w * ((gdouble)ph/pw);
            } else {
                nw = w * ((gdouble)pw/ph);
                nh = w;
            }

            pixbuf = p_free = gdk_pixbuf_scale_simple(pixbuf, nw, nh,
                                                      GDK_INTERP_BILINEAR);
        }
    }

    gtk_image_set_from_pixbuf(GTK_IMAGE(window->icon), pixbuf);

    if(pixbuf)
        gtk_widget_show(window->icon_box);
    else
        gtk_widget_hide(window->icon_box);

    if(window->bg_path) {
        cairo_path_destroy(window->bg_path);
        window->bg_path = NULL;
        gtk_widget_queue_draw(GTK_WIDGET(window));
    }

    if(p_free)
        g_object_unref(G_OBJECT(p_free));
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

    if(GTK_WIDGET_REALIZED(window)) {
        if(window->expire_id) {
            g_source_remove(window->expire_id);
            window->expire_id = 0;
        }
        xfce_notify_window_start_expiration(window);
    }
}

void
xfce_notify_window_set_actions(XfceNotifyWindow *window,
                               const gchar **actions)
{
    gint i;
    GList *children, *l;

    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    children = gtk_container_get_children(GTK_CONTAINER(window->button_box));
    for(l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    if(!actions) {
        gtk_widget_hide(window->button_box);
        window->has_actions = FALSE;
    } else {
        gtk_widget_show(window->button_box);
        window->has_actions = TRUE;
    }

    for(i = 0; actions && actions[i]; i += 2) {
        const gchar *cur_action_id = actions[i];
        const gchar *cur_button_text = actions[i+1];
        GtkWidget *btn, *lbl;
        gchar *cur_button_text_escaped;

        if(!cur_button_text || !cur_action_id || !*cur_action_id)
            break;

        btn = gtk_button_new();
        g_object_set_data_full(G_OBJECT(btn), "--action-id",
                               g_strdup(cur_action_id),
                               (GDestroyNotify)g_free);
        gtk_widget_show(btn);
        gtk_container_add(GTK_CONTAINER(window->button_box), btn);
        g_signal_connect(G_OBJECT(btn), "clicked",
                         G_CALLBACK(xfce_notify_window_button_clicked),
                         window);

        cur_button_text_escaped = g_markup_printf_escaped("<span size='small'>%s</span>",
                                                          cur_button_text);

        lbl = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(lbl), cur_button_text_escaped);
        gtk_label_set_use_markup(GTK_LABEL(lbl), TRUE);
        gtk_widget_show(lbl);
        gtk_container_add(GTK_CONTAINER(btn), lbl);

        g_free(cur_button_text_escaped);
    }

    if(window->bg_path) {
        cairo_path_destroy(window->bg_path);
        window->bg_path = NULL;
        gtk_widget_queue_draw(GTK_WIDGET(window));
    }
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

    window->normal_opacity = opacity;
    window->op_change_steps = FADE_TIME / FADE_CHANGE_TIMEOUT;
    window->op_change_delta = opacity / window->op_change_steps;

    if(GTK_WIDGET_REALIZED(window) && window->expire_id && !window->fade_id)
        gtk_window_set_opacity(GTK_WINDOW(window), window->normal_opacity);
}

gdouble
xfce_notify_window_get_opacity(XfceNotifyWindow *window)
{
    g_return_val_if_fail(XFCE_IS_NOTIFY_WINDOW(window), 0.0);
    return window->normal_opacity;
}

void
xfce_notify_window_set_icon_only(XfceNotifyWindow *window,
                                 gboolean icon_only)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    if(icon_only == window->icon_only)
        return;

    window->icon_only = !!icon_only;

    if(icon_only) {
        GtkRequisition req;

        if(!GTK_WIDGET_VISIBLE(window->icon_box)) {
            g_warning("Attempt to set icon-only mode with no icon");
            return;
        }

        gtk_widget_hide(window->content_box);

        /* set a wider size on the icon box so it takes up more space */
        gtk_widget_realize(window->icon);
        gtk_widget_size_request(window->icon, &req);
        gtk_widget_set_size_request(window->icon_box, req.width * 4, -1);
        /* and center it */
        gtk_alignment_set(GTK_ALIGNMENT(window->icon_box), 0.5, 0.0, 0.0, 0.0);
    } else {
        gtk_alignment_set(GTK_ALIGNMENT(window->icon_box), 0.0, 0.0, 0.0, 0.0);
        gtk_widget_set_size_request(window->icon_box, -1, -1);
        gtk_widget_show(window->content_box);
    }
}

void
xfce_notify_window_set_gauge_value(XfceNotifyWindow *window,
                                   gint value)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    /* maybe want to do some kind of effect if the value is out of bounds */
    if(value > 100)
        value = 100;
    else if(value < 0)
        value = 0;

    gtk_widget_hide(window->summary);
    gtk_widget_hide(window->body);
    gtk_widget_hide(window->button_box);

    if(!window->gauge) {
        GtkWidget *align;
        gint width;

        if(GTK_WIDGET_VISIBLE(window->icon)) {
            /* size the pbar in relation to the icon */
            GtkRequisition req;

            gtk_widget_realize(window->icon);
            gtk_widget_size_request(window->icon, &req);
            width = req.width * 4;
        } else {
            /* FIXME: do something less arbitrary */
            width = 120;
        }

        align = gtk_alignment_new(0.0, 0.5, 0.0, 0.0);
        gtk_widget_show(align);
        gtk_box_pack_start(GTK_BOX(window->content_box), align, TRUE, TRUE, 0);

        window->gauge = gtk_progress_bar_new();
        gtk_widget_set_size_request(window->gauge, width, -1);
        gtk_widget_show(window->gauge);
        gtk_container_add(GTK_CONTAINER(align), window->gauge);
    }

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(window->gauge),
                                  value / 100.0);
}

void
xfce_notify_window_unset_gauge_value(XfceNotifyWindow *window)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    if(window->gauge) {
        GtkWidget *align = gtk_widget_get_parent(window->gauge);

        g_assert(align);

        gtk_widget_destroy(align);
        window->gauge = NULL;

        if(window->has_summary_text)
            gtk_widget_show(window->summary);
        if(window->has_body_text)
            gtk_widget_show(window->body);
        if(window->has_actions)
            gtk_widget_show(window->button_box);
    }
}

void
xfce_notify_window_closed(XfceNotifyWindow *window,
                          XfceNotifyCloseReason reason)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window)
                     && reason >= XFCE_NOTIFY_CLOSE_REASON_EXPIRED
                     && reason <= XFCE_NOTIFY_CLOSE_REASON_UNKNOWN);

    g_signal_emit(G_OBJECT(window), signals[SIG_CLOSED], 0, reason);
}
