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

#include <libxfcegui4/libxfcegui4.h>

#ifdef HAVE_LIBSEXY
#include <libsexy/sexy.h>
#endif

#include "xfce-notify-window.h"
#include "xfce-notify-enum-types.h"

#define DEFAULT_EXPIRE_TIMEOUT    10000
#define DEFAULT_NORMAL_OPACITY        0.85
#define FADE_TIME                   800
#define FADE_CHANGE_TIMEOUT          50
#define DEFAULT_RADIUS               10.0
#define DEFAULT_BORDER_WIDTH          2.0
#define BORDER                        6

struct _XfceNotifyWindow
{
    GtkWindow parent;

    GdkRectangle geometry;
    gint last_monitor;
    gint last_screen;

    guint expire_timeout;

    gboolean mouse_hover;
    cairo_path_t *bg_path;
    cairo_path_t *close_btn_path;
    GdkRegion *close_btn_region;

    gdouble normal_opacity;

    guint32 fade_transparent:1,
            icon_only:1,
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
static void xfce_notify_window_style_set(GtkWidget *widget,
                                         GtkStyle *previous_style);

static gboolean xfce_notify_window_expire_timeout(gpointer data);
static gboolean xfce_notify_window_fade_timeout(gpointer data);

static void xfce_notify_window_button_clicked(GtkWidget *widget,
                                              gpointer user_data);

#ifdef HAVE_LIBSEXY
static void xfce_notify_window_url_clicked(SexyUrlLabel *label,
                                           const gchar *url,
                                           gpointer user_data);
#endif

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
    widget_class->style_set = xfce_notify_window_style_set;

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
                                            g_param_spec_boolean("summary-bold",
                                                                 "summary bold",
                                                                 "whether or not to display the notification summary field in bold text",
                                                                 FALSE,
                                                                 G_PARAM_READABLE));
}

static void
xfce_notify_window_init(XfceNotifyWindow *window)
{
    GdkScreen *screen;
    GtkWidget *tophbox, *align, *vbox;
    gdouble border_radius = DEFAULT_RADIUS;
    
    GTK_WINDOW(window)->type = GTK_WINDOW_TOPLEVEL;
    window->expire_timeout = DEFAULT_EXPIRE_TIMEOUT;
    window->normal_opacity = DEFAULT_NORMAL_OPACITY;
    window->fade_transparent = TRUE;

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
                         "border-radius", &border_radius,
                         NULL);

    tophbox = gtk_hbox_new(FALSE, BORDER);
    gtk_container_set_border_width(GTK_CONTAINER(tophbox), border_radius + 4);
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
    gtk_label_set_line_wrap(GTK_LABEL(window->summary), TRUE);
    gtk_misc_set_alignment(GTK_MISC(window->summary), 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), window->summary, FALSE, FALSE, 0);

#ifdef HAVE_LIBSEXY
    window->body = sexy_url_label_new();
#else
    window->body = gtk_label_new(NULL);
#endif
    gtk_label_set_line_wrap(GTK_LABEL(window->body), TRUE);
    gtk_misc_set_alignment(GTK_MISC(window->body), 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), window->body, TRUE, TRUE, 0);
#ifdef HAVE_LIBSEXY
    g_signal_connect(G_OBJECT(window->body), "url-activated",
                     G_CALLBACK(xfce_notify_window_url_clicked), window);
#endif

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
        g_get_current_time(&ct);
        window->expire_start_timestamp = ct.tv_sec * 1000 + ct.tv_usec / 1000;
        window->expire_id = g_timeout_add(window->fade_transparent
                                          ? window->expire_timeout - FADE_TIME
                                          : window->expire_timeout,
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

    if(window->close_btn_path) {
        cairo_path_destroy(window->close_btn_path);
        window->close_btn_path = NULL;
    }

    if(window->close_btn_region) {
        gdk_region_destroy(window->close_btn_region);
        window->close_btn_region = NULL;
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
    cairo_path_t *flat_path;
    GdkRegion *region;
    GdkFillRule fill_rule;
    GtkRequisition req;

    if(G_LIKELY(window->bg_path))
        return window->bg_path;

    gtk_widget_size_request(GTK_WIDGET(window), &req);

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

        if(window->mouse_hover) {
            /* but be sure to set the curved path because the code
            * below needs it */
            cairo_append_path(cr, bg_path);
        }
    }


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

        /* draw a circle with an X in it */
        if(!window->close_btn_path) {
            cairo_path_t *flat_path;
            GdkFillRule fill_rule;

            cairo_arc(cr, widget->allocation.width - 12., 12., 7.5, 0., 2*M_PI);
            window->close_btn_path = cairo_copy_path(cr);

            flat_path = cairo_copy_path_flat(cr);
            fill_rule = (cairo_get_fill_rule(cr) == CAIRO_FILL_RULE_WINDING
                         ? GDK_WINDING_RULE : GDK_EVEN_ODD_RULE);
            window->close_btn_region = xfce_gdk_region_from_cairo_flat_path(flat_path,
                                                                            fill_rule);
            cairo_path_destroy(flat_path);
        } else
            cairo_append_path(cr, window->close_btn_path);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);

        cairo_move_to(cr, widget->allocation.width - 8., 8.);
        cairo_line_to(cr, widget->allocation.width - 16., 16.);
        cairo_stroke(cr);
        cairo_move_to(cr, widget->allocation.width - 16., 8.);
        cairo_line_to(cr, widget->allocation.width - 8., 16.);
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
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(widget);

    if(!window->close_btn_region
       || !gdk_region_point_in(window->close_btn_region, evt->x, evt->y))
    {
        g_signal_emit(G_OBJECT(widget), signals[SIG_ACTION_INVOKED], 0,
                      "default");
    }

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

    if(window->close_btn_path) {
        cairo_path_destroy(window->close_btn_path);
        window->close_btn_path = NULL;
    }

    if(window->close_btn_region) {
        gdk_region_destroy(window->close_btn_region);
        window->close_btn_region = NULL;
    }

    gtk_widget_queue_draw(widget);

    return ret;
}

static void
xfce_notify_window_style_set(GtkWidget *widget,
                             GtkStyle *previous_style)
{
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(widget);
    gboolean summary_bold = FALSE;
    GtkStyle *style;
    PangoFontDescription *pfd;

    gtk_widget_style_get(widget,
                         "summary-bold", &summary_bold,
                         NULL);
    if(summary_bold) {
        style = gtk_widget_get_style(window->summary);
        pfd = pango_font_description_copy(style->font_desc);
        pango_font_description_set_weight(pfd, PANGO_WEIGHT_BOLD);
        gtk_widget_modify_font(window->summary, pfd);
        pango_font_description_free(pfd);
    }
}



static gboolean
xfce_notify_window_expire_timeout(gpointer data)
{
    XfceNotifyWindow *window = data;

    window->expire_id = 0;

    if(window->fade_transparent) {
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
    g_signal_emit(G_OBJECT(widget), signals[SIG_CLOSED], 0,
                  XFCE_NOTIFY_CLOSE_REASON_DISMISSED);
}

#ifdef HAVE_LIBSEXY
static void
xfce_notify_window_url_clicked(SexyUrlLabel *label,
                               const gchar *url,
                               gpointer user_data)
{
    gchar *opener, *url_quoted, *cmd = NULL;

    if(!(opener = g_find_program_in_path("xdg-open")))
        if(!(opener = g_find_program_in_path("exo-open")))
            if(!(opener = g_find_program_in_path("gnome-open")))
                opener = g_find_program_in_path("firefox");

    if(opener) {
        url_quoted = g_shell_quote(url);
        cmd = g_strdup_printf("%s %s", opener, url_quoted);
        xfce_exec(cmd, FALSE, FALSE, NULL);
        g_free(url_quoted);
        g_free(cmd);
        g_free(opener);
    }
}
#endif

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

static gchar *
xfce_notify_window_validate_escape_markup(const gchar *str)
{
    GString *gstr;
    const gchar *p;
    GQueue *open_elems;
    gconstpointer tmp;

    if(!str)
        return NULL;

    open_elems = g_queue_new();
    gstr = g_string_sized_new(strlen(str));
    p = str;

    while(*p) {
        if('<' == *p) {
            if('b' == *(p+1) && '>' == *(p+2)) {
                g_queue_push_head(open_elems, ELEM_B);
                g_string_append(gstr, "<b>");
                p += 3;
            } else if('i' == *(p+1) && '>' == *(p+2)) {
                g_queue_push_head(open_elems, ELEM_I);
                g_string_append(gstr, "<i>");
                p += 3;
            } else if('u' == *(p+1) && '>' == *(p+2)) {
                g_queue_push_head(open_elems, ELEM_U);
                g_string_append(gstr, "<u>");
                p += 3;
            } else if('a' == *(p+1) && ' ' == *(p+2)) {
                gchar *aend;

                g_queue_push_head(open_elems, ELEM_A);

                aend = strchr(p+3, '>');
                if(!aend) {
                    g_warning("Bad markup in <a>: %s", str);
                    goto out_err;
                }
#ifdef HAVE_LIBSEXY
                /* only support links with SexyUrlLabel*/
                g_string_append_len(gstr, p, aend - p + 1);
#endif
                p = aend + 1;
            } else if(!strncmp(p+1, "img ", 4)) {
                /* don't currently support images; extract alt text
                 * if available */
                gchar *imgend, *altbegin, *altend = NULL;

                altbegin = strstr(p+5, "alt=\"");
                if(altbegin) {
                    altbegin += 5;
                    altend = strchr(altbegin, '"');
                    if(!altend) {
                        g_warning("End of <img> alt text not found");
                        goto out_err;
                    }
                }

                imgend = strstr(altend ? altend+1 : p+4, "/>");
                if(!imgend) {
                    g_warning("Unclosed <img> tag");
                    goto out_err;
                }

                if(altbegin) {
                    /* put the alt text in the label */
                    g_string_append_c(gstr, '[');
                    g_string_append(gstr, _("image: "));
                    p = altbegin;
                    while(p < altend) {
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
                }

                p = imgend + 2;
            } else if('/' == *(p+1)) {
                if('b' == *(p+2) && '>' == *(p+3)) {
                    tmp = g_queue_pop_head(open_elems);
                    if(tmp != ELEM_B) {
                        g_warning("Bad markup: closing <b> when %s expected",
                                  elem_to_string(tmp));
                        goto out_err;
                    }
                    g_string_append(gstr, "</b>");
                    p += 4;
                } else if('i' == *(p+2) && '>' == *(p+3)) {
                    tmp = g_queue_pop_head(open_elems);
                    if(tmp != ELEM_I) {
                        g_warning("Bad markup: closing <i> when %s expected",
                                  elem_to_string(tmp));
                        goto out_err;
                    }
                    g_string_append(gstr, "</i>");
                    p += 4;
                } else if('u' == *(p+2) && '>' == *(p+3)) {
                    tmp = g_queue_pop_head(open_elems);
                    if(tmp != ELEM_U) {
                        g_warning("Bad markup: closing <u> when %s expected",
                                  elem_to_string(tmp));
                        goto out_err;
                    }
                    g_string_append(gstr, "</u>");
                    p += 4;
                } else if('a' == *(p+2) && '>' == *(p+3)) {
                    tmp = g_queue_pop_head(open_elems);
                    if(tmp != ELEM_A) {
                        g_warning("Bad markup: closing <a> when %s expected",
                                  elem_to_string(tmp));
                        goto out_err;
                    }
#ifdef HAVE_LIBSEXY
                    g_string_append(gstr, "</a>");
#endif
                    p += 4;
                } else {
                    g_string_append(gstr, "&gt;");
                    p++;
                }
            } else {
                g_string_append(gstr, "&lt;");
                p++;
            }
        } else if('>' == *p) {
            g_string_append(gstr, "&gt;");
            p++;
        } else {
            const gchar *next = g_utf8_next_char(p);

            if(!next) {
                g_critical("Bad UTF-8 in string");
                goto out_err;
            }

            g_string_append_len(gstr, p, next - p);
            p = next;
        }
    }

    p = gstr->str;
    g_string_free(gstr, FALSE);
    g_queue_free(open_elems);

    return (gchar *)p;

out_err:
    g_string_free(gstr, TRUE);
    g_queue_free(open_elems);
    return NULL;
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
        gchar *markup = xfce_notify_window_validate_escape_markup(body);
        if(!markup)
            return;
#ifdef HAVE_LIBSEXY
        sexy_url_label_set_markup(SEXY_URL_LABEL(window->body), markup);
#else
        gtk_label_set_markup(GTK_LABEL(window->body), markup);
        gtk_label_set_use_markup(GTK_LABEL(window->body), TRUE);
#endif
        gtk_widget_show(window->body);
        g_free(markup);
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
    
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    if(icon_name && *icon_name) {
        gint w, h;
        GdkPixbuf *pix;
        
        gtk_icon_size_lookup(GTK_ICON_SIZE_DIALOG, &w, &h);
        pix = xfce_themed_icon_load(icon_name, w);
        if(pix) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(window->icon), pix);
            gtk_widget_show(window->icon_box);
            g_object_unref(G_OBJECT(pix));
            icon_set = TRUE;
        }
    }
    
    if(!icon_set) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(window->icon), NULL);
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
xfce_notify_window_set_fade_transparent(XfceNotifyWindow *window,
                                        gboolean fade_transparent)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    if(fade_transparent == window->fade_transparent)
        return;

    window->fade_transparent = !!fade_transparent;

    /* if we're already realized, we don't actually do anything here */
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
