/*
 *  xfce4-notifyd
 *
 *  Copyright (c) 2008-2009 Brian Tarricone <bjt23@cornell.edu>
 *  Copyright (c) 2009 Jérôme Guelfucci <jeromeg@xfce.org>
 *  Copyright (c) 2015 Ali Abdallah    <ali@xfce.org>
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

#ifdef ENABLE_WAYLAND
#include <gdk/gdkwayland.h>
#include <gtk-layer-shell.h>
#endif

#ifdef ENABLE_SOUND
#include <canberra-gtk.h>
#endif

#include <common/xfce-notify-common.h>

#include "xfce-notify-window.h"

#define DEFAULT_EXPIRE_TIMEOUT 10000
#define DEFAULT_NORMAL_OPACITY 0.85
#define DEFAULT_DO_FADEOUT     TRUE
#define DEFAULT_DO_SLIDEOUT    FALSE
#define FADE_TIME              800
#define FADE_CHANGE_TIMEOUT    50
#define DEFAULT_RADIUS         10
#define DEFAULT_PADDING        14.0
#define BASE_CSS               ".xfce4-notifyd { font-size: initial; }"
#define NO_COMPOSITING_CSS     ".xfce4-notifyd { border-radius: 0px; }"

struct _XfceNotifyWindow
{
    GtkWindow parent;

    guint id;
    gchar *log_id;

    GdkRectangle geometry;
    gint last_monitor;
    gboolean override_redirect;

    guint expire_timeout;

    gboolean mouse_hover;

    gdouble normal_opacity;
    gint original_x, original_y;

    guint32 icon_only:1,
            has_summary_text:1,
            has_body_text:1,
            has_actions:1;

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
    gboolean do_fadeout;
    gboolean do_slideout;
    GtkCornerType notify_location;

#ifdef ENABLE_SOUND
    ca_proplist *sound_props;
#endif
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
static gboolean xfce_notify_window_draw (GtkWidget *widget,
                                         cairo_t *cr);
static gboolean xfce_notify_window_enter_leave(GtkWidget *widget,
                                               GdkEventCrossing *evt);
static gboolean xfce_notify_window_button_release(GtkWidget *widget,
                                                  GdkEventButton *evt);
static gboolean xfce_notify_window_motion_notify(GtkWidget *widget,
                                                 GdkEventMotion *evt);
static gboolean xfce_notify_window_configure_event(GtkWidget *widget,
                                                   GdkEventConfigure *evt);
static gboolean xfce_notify_window_expire_timeout(gpointer data);
static gboolean xfce_notify_window_fade_timeout(gpointer data);

static void xfce_notify_window_button_clicked(GtkWidget *widget,
                                              gpointer user_data);

static guint signals[N_SIGS] = { 0, };

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

G_DEFINE_TYPE(XfceNotifyWindow, xfce_notify_window, GTK_TYPE_WINDOW)


static void
xfce_notify_window_class_init(XfceNotifyWindowClass *klass)
{
    GObjectClass *gobject_class = (GObjectClass *)klass;
    GtkWidgetClass *widget_class = (GtkWidgetClass *)klass;

    gobject_class->finalize = xfce_notify_window_finalize;

    widget_class->realize = xfce_notify_window_realize;
    widget_class->unrealize = xfce_notify_window_unrealize;

    widget_class->draw = xfce_notify_window_draw;
    widget_class->enter_notify_event = xfce_notify_window_enter_leave;
    widget_class->leave_notify_event = xfce_notify_window_enter_leave;
    widget_class->motion_notify_event =xfce_notify_window_motion_notify;
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
    GtkWidget *topvbox, *tophbox, *vbox;
    gint screen_width;
    gdouble padding = DEFAULT_PADDING;
    GtkCssProvider *provider;
    GdkMonitor *monitor;
    GdkRectangle geometry;

    window->expire_timeout = DEFAULT_EXPIRE_TIMEOUT;
    window->normal_opacity = DEFAULT_NORMAL_OPACITY;
    window->do_fadeout = DEFAULT_DO_FADEOUT;
    window->do_slideout = DEFAULT_DO_SLIDEOUT;

#ifdef ENABLE_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default())) {
        gtk_layer_init_for_window(GTK_WINDOW(window));
        gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_TOP);
        gtk_layer_set_namespace(GTK_WINDOW(window), "notification");
        gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    }
#endif

    gtk_widget_set_name (GTK_WIDGET(window), "XfceNotifyWindow");
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
        GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
        if (visual == NULL)
            visual = gdk_screen_get_system_visual (screen);

        gtk_widget_set_visual (GTK_WIDGET(window), visual);
    }

    /* Use the monitor width to get a maximum width for the notification bubble.
       This assumes that a character is 10px wide and we want a third of the
       monitor as maximum width. */
    monitor = gdk_display_get_monitor_at_window (gtk_widget_get_display (GTK_WIDGET (window)),
                                                 gdk_screen_get_root_window (screen));
    gdk_monitor_get_geometry (monitor, &geometry);
    screen_width = geometry.width / 30;

    gtk_widget_style_get(GTK_WIDGET(window),
                         "padding", &padding,
                         NULL);

    topvbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_set_homogeneous (GTK_BOX (topvbox), FALSE);
    gtk_container_set_border_width (GTK_CONTAINER(topvbox), padding);
    gtk_widget_show (topvbox);
    gtk_container_add (GTK_CONTAINER(window), topvbox);
    tophbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_set_homogeneous (GTK_BOX (tophbox), FALSE);
    gtk_widget_show (tophbox);
    gtk_container_add (GTK_CONTAINER(topvbox), tophbox);

    window->icon_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(window->icon_box), 0);
    gtk_widget_set_margin_end (GTK_WIDGET (window->icon_box), padding);

    gtk_box_pack_start(GTK_BOX(tophbox), window->icon_box, FALSE, TRUE, 0);

    window->icon = gtk_image_new();
    gtk_widget_show(window->icon);
    gtk_container_add(GTK_CONTAINER(window->icon_box), window->icon);

    window->content_box = vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_set_homogeneous(GTK_BOX (vbox), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 0);
    gtk_widget_show(vbox);
    gtk_box_pack_start(GTK_BOX(tophbox), vbox, TRUE, TRUE, 0);

    window->summary = gtk_label_new(NULL);
    gtk_widget_set_name (window->summary, "summary");
    gtk_label_set_max_width_chars (GTK_LABEL(window->summary), screen_width);
    gtk_label_set_ellipsize (GTK_LABEL (window->summary), PANGO_ELLIPSIZE_END);
    gtk_label_set_line_wrap (GTK_LABEL(window->summary), TRUE);
    gtk_label_set_line_wrap_mode (GTK_LABEL (window->summary), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_lines (GTK_LABEL (window->summary), 1);
    gtk_widget_set_halign (window->summary, GTK_ALIGN_FILL);
    gtk_label_set_xalign (GTK_LABEL(window->summary), 0);
    gtk_widget_set_valign (window->summary, GTK_ALIGN_BASELINE);
    gtk_box_pack_start(GTK_BOX(vbox), window->summary, TRUE, TRUE, 0);

    window->body = gtk_label_new(NULL);
    gtk_widget_set_name (window->body, "body");
    gtk_label_set_max_width_chars (GTK_LABEL(window->body), screen_width);
    gtk_label_set_ellipsize (GTK_LABEL (window->body), PANGO_ELLIPSIZE_END);
    gtk_label_set_line_wrap (GTK_LABEL(window->body), TRUE);
    gtk_label_set_line_wrap_mode (GTK_LABEL (window->body), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_lines (GTK_LABEL (window->body), 6);
    gtk_widget_set_halign (window->body, GTK_ALIGN_FILL);
    gtk_label_set_xalign (GTK_LABEL(window->body), 0);
    gtk_widget_set_valign (window->body, GTK_ALIGN_BASELINE);
    gtk_box_pack_start(GTK_BOX(vbox), window->body, TRUE, TRUE, 0);

    window->button_box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(window->button_box),
                              GTK_BUTTONBOX_END);
    gtk_box_set_spacing (GTK_BOX(window->button_box), padding / 2);
    gtk_box_set_homogeneous (GTK_BOX(window->button_box), FALSE);
    gtk_box_pack_end (GTK_BOX(topvbox), window->button_box, FALSE, FALSE, 0);

    gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (window)), "xfce4-notifyd");
    provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (provider, BASE_CSS, -1, NULL);
    gtk_style_context_add_provider (gtk_widget_get_style_context (GTK_WIDGET (window)),
                                    GTK_STYLE_PROVIDER (provider),
                                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref (provider);
}

static void
xfce_notify_window_start_expiration(XfceNotifyWindow *window)
{
    if(window->expire_timeout) {
        gint64 ct;
        guint timeout;
        gboolean fade_transparent;

        ct = g_get_real_time();

        fade_transparent =
            gdk_screen_is_composited(gtk_window_get_screen(GTK_WINDOW (window)));

        if(!fade_transparent)
            timeout = window->expire_timeout;
        else if(window->expire_timeout > FADE_TIME)
            timeout = window->expire_timeout - FADE_TIME;
        else
            timeout = FADE_TIME;

        window->expire_start_timestamp = ct / 1000;
        window->expire_id = g_timeout_add(timeout,
                                          xfce_notify_window_expire_timeout,
                                          window);
    }
    gtk_widget_set_opacity(GTK_WIDGET(window), window->normal_opacity);
}

static void
xfce_notify_window_finalize(GObject *object)
{
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(object);

#ifdef ENABLE_SOUND
    if (window->sound_props != NULL) {
        ca_proplist_destroy(window->sound_props);
    }
#endif

    g_free(window->log_id);

    G_OBJECT_CLASS(xfce_notify_window_parent_class)->finalize(object);
}

static void
xfce_notify_window_realize(GtkWidget *widget)
{
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(widget);

#ifdef ENABLE_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default())) {
        gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, window->geometry.x);
        gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, window->geometry.y);
    }
#endif

    GTK_WIDGET_CLASS(xfce_notify_window_parent_class)->realize(widget);

    gdk_window_set_override_redirect(gtk_widget_get_window(widget), window->override_redirect);

    xfce_notify_window_start_expiration(window);

#ifdef ENABLE_SOUND
    if (window->sound_props != NULL) {
        ca_context *ctx;
        const gchar *summary;

        summary = gtk_label_get_text(GTK_LABEL(window->summary));
        if (summary != NULL) {
            ca_proplist_sets(window->sound_props, CA_PROP_EVENT_DESCRIPTION, summary);
        }
        ca_gtk_proplist_set_for_widget(window->sound_props, widget);

        ctx = ca_gtk_context_get_for_screen(gtk_widget_get_screen(widget));
        ca_context_play_full(ctx, window->id, window->sound_props, NULL, NULL);
    }
#endif
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

}

static gboolean
xfce_notify_window_draw (GtkWidget *widget,
                         cairo_t *cr)
{
    GtkStyleContext  *context;
    GtkAllocation     allocation;
    GdkScreen        *screen;
    GtkCssProvider   *provider;
    GtkStateFlags     state;
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW (widget);

    state = GTK_STATE_FLAG_NORMAL;
    if (window->mouse_hover)
        state = GTK_STATE_FLAG_PRELIGHT;

    context = gtk_widget_get_style_context (widget);
    gtk_widget_get_allocation (widget, &allocation);

    /* Remove rounded corners when compositing is disabled */
    screen = gtk_widget_get_screen (widget);
    if (!gdk_screen_is_composited (screen)) {
        provider = gtk_css_provider_new ();
        gtk_css_provider_load_from_data (provider, NO_COMPOSITING_CSS, -1, NULL);
        gtk_style_context_add_provider (context,
                                        GTK_STYLE_PROVIDER (provider),
                                        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref (provider);
    }

    /* First make the window transparent */
    cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.0);
    cairo_fill (cr);

    /* Then render the background and border based on the Gtk theme */
    gtk_style_context_set_state (context, state);
    gtk_render_background (context, cr, allocation.x, allocation.y, allocation.width, allocation.height);
    gtk_render_frame (context, cr, allocation.x, allocation.y, allocation.width, allocation.height);

    /* Then draw the rest of the window */
    GTK_WIDGET_CLASS (xfce_notify_window_parent_class)->draw (widget, cr);

    return FALSE;
}

static gboolean
xfce_notify_window_enter_leave(GtkWidget *widget,
                               GdkEventCrossing *evt)
{
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(widget);

    if (evt->type == GDK_LEAVE_NOTIFY && evt->detail != GDK_NOTIFY_INFERIOR) {
        if (window->expire_id == 0) {
            xfce_notify_window_start_expiration(window);
        }
        window->mouse_hover = FALSE;
        gtk_widget_queue_draw(widget);
    }

    return FALSE;
}

static gboolean
xfce_notify_window_motion_notify(GtkWidget *widget,
                                 GdkEventMotion *evt)
{
    XfceNotifyWindow *window = XFCE_NOTIFY_WINDOW(widget);

    if (window->expire_timeout != 0) {
        if (window->expire_id != 0) {
            g_source_remove(window->expire_id);
            window->expire_id = 0;
        }

        if (window->fade_id != 0) {
            g_source_remove(window->fade_id);
            window->fade_id = 0;

            /* reset the sliding-out window to its original position */
            if (window->do_slideout) {
#ifdef ENABLE_WAYLAND
                if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default())) {
                    gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, window->original_x);
                    gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, window->original_y);
                } else
#endif
                {
                    gtk_window_move (GTK_WINDOW (window), window->original_x, window->original_y);
                }
            }
        }
    }

    if (!window->mouse_hover) {
        gtk_widget_set_opacity(GTK_WIDGET(widget), 1.0);
        window->mouse_hover = TRUE;
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
    gboolean ret;

    ret = GTK_WIDGET_CLASS(xfce_notify_window_parent_class)->configure_event(widget,
                                                                             evt);

    gtk_widget_queue_draw(widget);

    return ret;
}



static gboolean
xfce_notify_window_expire_timeout(gpointer data)
{
    XfceNotifyWindow *window = data;
    gboolean          fade_transparent;
    gint              animation_timeout;

    g_return_val_if_fail(XFCE_IS_NOTIFY_WINDOW(data), FALSE);

    window->expire_id = 0;

    fade_transparent =
        gdk_screen_is_composited(gtk_window_get_screen(GTK_WINDOW(window)));

    if(fade_transparent && window->do_fadeout) {
        /* remember the original position of the window before we slide it out */
        if (window->do_slideout) {
#ifdef ENABLE_WAYLAND
            if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default())) {
                window->original_x = gtk_layer_get_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT);
                window->original_y = gtk_layer_get_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP);
            } else
#endif
            {
                gtk_window_get_position (GTK_WINDOW (window), &window->original_x, &window->original_y);
            }
            animation_timeout = FADE_CHANGE_TIMEOUT / 2;
        }
        else
            animation_timeout = FADE_CHANGE_TIMEOUT;
        window->fade_id = g_timeout_add(animation_timeout,
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
    gdouble op;
    gint x, y;

    g_return_val_if_fail(XFCE_IS_NOTIFY_WINDOW(data), FALSE);

    /* slide out animation */
    if (window->do_slideout) {
#ifdef ENABLE_WAYLAND
        if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default())) {
            x = gtk_layer_get_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT);
            y = gtk_layer_get_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP);
        } else
#endif
        {
            gtk_window_get_position (GTK_WINDOW (window), &x, &y);
        }

        if (window->notify_location == GTK_CORNER_TOP_RIGHT ||
            window->notify_location == GTK_CORNER_BOTTOM_RIGHT)
            x = x + 10;
        else if (window->notify_location == GTK_CORNER_TOP_LEFT ||
            window->notify_location == GTK_CORNER_BOTTOM_LEFT)
            x = x - 10;
        else
            g_warning("Invalid notify location: %d", window->notify_location);


#ifdef ENABLE_WAYLAND
        if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default())) {
            gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, x);
            gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, y);
        } else
#endif
        {
            gtk_window_move (GTK_WINDOW (window), x, y);
        }
    }

    /* fade-out animation */
    op = gtk_widget_get_opacity(GTK_WIDGET(window));
    op -= window->op_change_delta;
    if(op < 0.0)
        op = 0.0;

    gtk_widget_set_opacity(GTK_WIDGET(window), op);

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
    XfceNotifyWindow *window;
    gchar *action_id;

    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(user_data));

    window = XFCE_NOTIFY_WINDOW(user_data);

    action_id = g_object_get_data(G_OBJECT(widget), "--action-id");
    g_assert(action_id);

    g_signal_emit(G_OBJECT(window), signals[SIG_ACTION_INVOKED], 0,
                  action_id);
    g_signal_emit(G_OBJECT(window), signals[SIG_CLOSED], 0,
                  XFCE_NOTIFY_CLOSE_REASON_DISMISSED);
}

GtkWidget *
xfce_notify_window_new(void)
{
    return xfce_notify_window_new_with_actions(NULL, NULL, NULL, -1, NULL, FALSE, NULL);
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
                                               NULL,
                                               FALSE,
                                               NULL);
}

GtkWidget *
xfce_notify_window_new_with_actions(const gchar *summary,
                                    const gchar *body,
                                    const gchar *icon_name,
                                    gint expire_timeout,
                                    const gchar **actions,
                                    gboolean actions_are_icon_names,
                                    GtkCssProvider *css_provider)
{
    XfceNotifyWindow *window;

    window = g_object_new(XFCE_TYPE_NOTIFY_WINDOW,
                          "type", GTK_WINDOW_TOPLEVEL, NULL);

    window->original_x = -1;
    window->original_y = -1;

    xfce_notify_window_set_summary(window, summary);
    xfce_notify_window_set_body(window, body);
    xfce_notify_window_set_icon_name(window, icon_name);
    xfce_notify_window_set_expire_timeout(window, expire_timeout);
    xfce_notify_window_set_actions(window, actions, actions_are_icon_names, css_provider);

    return GTK_WIDGET(window);
}

void
xfce_notify_window_set_id(XfceNotifyWindow *window,
                          guint id)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));
    window->id = id;
}

guint
xfce_notify_window_get_id(XfceNotifyWindow *window)
{
    g_return_val_if_fail(XFCE_IS_NOTIFY_WINDOW(window), 0);
    return window->id;
}

void
xfce_notify_window_set_log_id(XfceNotifyWindow *window,
                              const gchar *id)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    g_free(window->log_id);
    window->log_id = g_strdup(id);
}

const gchar *
xfce_notify_window_get_log_id(XfceNotifyWindow *window)
{
    g_return_val_if_fail(XFCE_IS_NOTIFY_WINDOW(window), NULL);
    return window->log_id;
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

    if(gtk_widget_get_realized(GTK_WIDGET(window)))
        gtk_widget_queue_draw(GTK_WIDGET(window));
}

void
xfce_notify_window_set_body(XfceNotifyWindow *window,
                            const gchar *body)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    if(body && *body) {
        if (xfce_notify_is_markup_valid(body)) {
            gtk_label_set_markup (GTK_LABEL (window->body), body);
        } else {
            gtk_label_set_text(GTK_LABEL(window->body), body);
        }
        gtk_widget_show(window->body);
        window->has_body_text = TRUE;
    } else {
        gtk_label_set_markup(GTK_LABEL(window->body), "");
        gtk_widget_hide(window->body);
        window->has_body_text = FALSE;
    }

    if(gtk_widget_get_realized(GTK_WIDGET(window)))
        gtk_widget_queue_draw(GTK_WIDGET(window));
}

void
xfce_notify_window_set_geometry(XfceNotifyWindow *window,
                                GdkRectangle rectangle)
{
    window->geometry = rectangle;

#ifdef ENABLE_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default())) {
        gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, window->geometry.x);
        gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, window->geometry.y);
    }
#endif
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
xfce_notify_window_set_icon_name (XfceNotifyWindow *window,
                                  const gchar *icon_name)
{
    GIcon *icon = NULL;

    g_return_if_fail (XFCE_IS_NOTIFY_WINDOW (window));

    if (icon_name && *icon_name) {
        gboolean is_absolute, is_uri;

        is_absolute = g_path_is_absolute (icon_name);
        is_uri = g_str_has_prefix(icon_name, "file://");

        if (is_absolute || is_uri) {
            GFile *file = is_absolute ? g_file_new_for_path (icon_name) : g_file_new_for_uri (icon_name);

            if (g_file_query_exists (file, NULL) && g_file_query_file_type (file, G_FILE_QUERY_INFO_NONE, NULL) == G_FILE_TYPE_REGULAR) {
                icon = g_file_icon_new (file);
            }
            g_object_unref (file);
        } else {
            icon = g_themed_icon_new_with_default_fallbacks (icon_name);
        }
    }

    if (icon != NULL) {
        gtk_image_set_from_gicon (GTK_IMAGE (window->icon), icon, GTK_ICON_SIZE_DIALOG);
        gtk_widget_show (window->icon_box);
        g_object_unref (icon);
    } else {
        gtk_image_clear (GTK_IMAGE (window->icon));
        gtk_widget_hide (window->icon_box);
    }

    if (gtk_widget_get_realized(GTK_WIDGET(window))) {
        gtk_widget_queue_draw(GTK_WIDGET(window));
    }
}

void
xfce_notify_window_set_icon_pixbuf(XfceNotifyWindow *window,
                                   GdkPixbuf *pixbuf)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window)
                     && (!pixbuf || GDK_IS_PIXBUF(pixbuf)));

    if(pixbuf) {
        GdkWindow *for_window = gtk_widget_get_window(GTK_WIDGET(window));
        gint scale_factor = gtk_widget_get_scale_factor(GTK_WIDGET(window));
        cairo_surface_t *surface;
        gint w, h, size, pw, ph;

        gtk_icon_size_lookup(GTK_ICON_SIZE_DIALOG, &w, &h);
        size = MIN(w, h) * scale_factor;
        pw = gdk_pixbuf_get_width(pixbuf);
        ph = gdk_pixbuf_get_height(pixbuf);

        if(pw > size || ph > size) {
            GdkPixbuf *pix_scaled;
            gint nw, nh;

            if(pw > ph) {
                nw = size;
                nh = size * ((gdouble)ph/pw);
            } else {
                nw = size * ((gdouble)pw/ph);
                nh = size;
            }

            pix_scaled = gdk_pixbuf_scale_simple(pixbuf, nw, nh,
                                                 GDK_INTERP_BILINEAR);
            surface = gdk_cairo_surface_create_from_pixbuf(pix_scaled, scale_factor, for_window);
            g_object_unref(pix_scaled);
        } else {
            surface = gdk_cairo_surface_create_from_pixbuf(pixbuf, scale_factor, for_window);
        }

        gtk_image_set_from_surface(GTK_IMAGE(window->icon), surface);
        gtk_widget_show(window->icon_box);
        cairo_surface_destroy(surface);
    } else {
        gtk_image_clear(GTK_IMAGE(window->icon));
        gtk_widget_hide(window->icon_box);
    }

    if(gtk_widget_get_realized(GTK_WIDGET(window))) {
        gtk_widget_queue_draw(GTK_WIDGET(window));
    }
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

    if(gtk_widget_get_realized(GTK_WIDGET(window))) {
        if(window->expire_id) {
            g_source_remove(window->expire_id);
            window->expire_id = 0;
        }
        if(window->fade_id) {
            g_source_remove(window->fade_id);
            window->fade_id = 0;
        }
        gtk_widget_set_opacity(GTK_WIDGET(window), window->normal_opacity);
        /* reset the sliding-out window to its original position */
        if (window->do_slideout && window->original_x >= 0) {
#ifdef ENABLE_WAYLAND
            if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default())) {
                gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, window->original_x);
                gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, window->original_y);
            } else
#endif
            {
                gtk_window_move (GTK_WINDOW (window), window->original_x, window->original_y);
            }
        }

        xfce_notify_window_start_expiration (window);
    }
}

void
xfce_notify_window_set_actions(XfceNotifyWindow *window,
                               const gchar **actions,
                               gboolean actions_are_icon_names,
                               GtkCssProvider *css_provider)
{
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
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
        GtkWidget *btn, *img = NULL, *lbl;
        gdouble padding;

        if(!cur_button_text || !cur_action_id || !*cur_action_id)
            break;
        /* Gnome applications seem to send a "default" action which often has no
           label or text, because it is intended to be executed when clicking
           the notification window.
           See https://developer.gnome.org/notification-spec/
           As we do not support this for the moment we hide buttons without labels. */
        if (g_strcmp0 (cur_button_text, "") == 0)
            continue;

        gtk_widget_style_get(GTK_WIDGET(window),
                             "padding", &padding,
                             NULL);
        btn = gtk_button_new();
        g_object_set_data_full(G_OBJECT(btn), "--action-id",
                               g_strdup(cur_action_id),
                               (GDestroyNotify)g_free);
        gtk_widget_show(btn);
        gtk_widget_set_margin_top (btn, padding / 2);
        gtk_container_add(GTK_CONTAINER(window->button_box), btn);
        g_signal_connect(G_OBJECT(btn), "clicked",
                         G_CALLBACK(xfce_notify_window_button_clicked),
                         window);

        if (actions_are_icon_names && gtk_icon_theme_has_icon(icon_theme, cur_action_id)) {
            GIcon *icon = g_themed_icon_new_with_default_fallbacks(cur_action_id);

            img = gtk_image_new_from_gicon(icon, GTK_ICON_SIZE_BUTTON);
            gtk_widget_set_tooltip_text(img, cur_button_text);
            atk_object_set_description(gtk_widget_get_accessible(img), cur_button_text);
            gtk_widget_show(img);
            gtk_container_add(GTK_CONTAINER(btn), img);

            gtk_widget_set_tooltip_text(btn, cur_button_text);

            g_object_unref(icon);
        }

        if (img == NULL) {
            gchar *cur_button_text_escaped = g_markup_printf_escaped("<span size='small'>%s</span>",
                                                                     cur_button_text);

            lbl = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(lbl), cur_button_text_escaped);
            gtk_label_set_use_markup(GTK_LABEL(lbl), TRUE);
            gtk_widget_show(lbl);
            gtk_container_add(GTK_CONTAINER(btn), lbl);
            gtk_style_context_add_provider (gtk_widget_get_style_context (btn),
                                            GTK_STYLE_PROVIDER (css_provider),
                                            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

            g_free(cur_button_text_escaped);
        }
    }

    if(gtk_widget_get_realized(GTK_WIDGET(window)))
        gtk_widget_queue_draw(GTK_WIDGET(window));
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

    if(gtk_widget_get_realized(GTK_WIDGET(window)) && window->expire_id && !window->fade_id)
        gtk_widget_set_opacity(GTK_WIDGET(window), window->normal_opacity);
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

        if(!gtk_widget_get_visible(window->icon_box)) {
            g_warning("Attempt to set icon-only mode with no icon");
            return;
        }

        gtk_widget_hide(window->content_box);

        /* set a wider size on the icon box so it takes up more space */
        gtk_widget_realize(window->icon);
        gtk_widget_get_preferred_size (window->icon, NULL, &req);
        gtk_widget_set_size_request(window->icon_box, req.width * 4, -1);
        /* and center it */
        g_object_set (window->icon_box,
                      "halign", GTK_ALIGN_CENTER,
                      NULL);
    } else {
        g_object_set (window->icon_box,
                      "halign", GTK_ALIGN_START,
                      NULL);
        gtk_widget_set_size_request(window->icon_box, -1, -1);
        gtk_widget_show(window->content_box);
    }
}

void
xfce_notify_window_set_gauge_value(XfceNotifyWindow *window,
                                   gint value,
                                   GtkCssProvider *css_provider)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    /* maybe want to do some kind of effect if the value is out of bounds */
    if(value > 100)
        value = 100;
    else if(value < 0)
        value = 0;

    gtk_widget_hide(window->button_box);

    if(!window->gauge) {
        GtkWidget *box;
        gint width;

        if(gtk_widget_get_visible(window->icon)) {
            /* size the pbar in relation to the icon */
            GtkRequisition req;

            gtk_widget_realize(window->icon);
            gtk_widget_get_preferred_size(window->icon, NULL, &req);
            width = req.width * 4;
        } else {
            /* FIXME: do something less arbitrary */
            width = 120;
        }

        box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_show(box);

        g_object_set(box, "valign", GTK_ALIGN_CENTER, NULL);
        gtk_box_pack_start(GTK_BOX(window->content_box), box, TRUE, TRUE, 0);

        window->gauge = gtk_progress_bar_new();
        gtk_widget_set_size_request(window->gauge, width, -1);
        gtk_widget_show(window->gauge);
        gtk_container_add(GTK_CONTAINER(box), window->gauge);
        gtk_style_context_add_provider (gtk_widget_get_style_context (window->gauge),
                                        GTK_STYLE_PROVIDER (css_provider),
                                        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
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
xfce_notify_window_set_do_fadeout(XfceNotifyWindow *window,
                                  gboolean do_fadeout,
                                  gboolean do_slideout)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    window->do_fadeout = do_fadeout;
    window->do_slideout = do_slideout;
}

void xfce_notify_window_set_notify_location(XfceNotifyWindow *window,
                                            GtkCornerType notify_location)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    window->notify_location = notify_location;
}

#ifdef ENABLE_SOUND
void
xfce_notify_window_set_sound_props(XfceNotifyWindow *window,
                                   ca_proplist *props)
{
    if (window->sound_props != props) {
        if (window->sound_props != NULL) {
            ca_proplist_destroy(window->sound_props);
        }

        window->sound_props = props;
    }
}
#endif

void
xfce_notify_window_set_override_redirect(XfceNotifyWindow *window,
                                         gboolean override_redirect)
{
    g_return_if_fail(XFCE_IS_NOTIFY_WINDOW(window));

    if (window->override_redirect != override_redirect) {
        window->override_redirect = override_redirect;

        if (gtk_widget_get_realized(GTK_WIDGET(window))) {
            gdk_window_set_override_redirect(gtk_widget_get_window(GTK_WIDGET(window)),
                                             window->override_redirect);
        }
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
