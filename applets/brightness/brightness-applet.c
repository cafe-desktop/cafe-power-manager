/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * CAFE Power Manager Brightness Applet
 * Copyright (C) 2006 Benjamin Canou <bookeldor@gmail.com>
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cafe-panel-applet.h>
#include <ctk/ctk.h>
#include <glib/gi18n.h>
#include <cdk/cdkkeysyms.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>

#include "egg-debug.h"
#include "cpm-common.h"

#define GPM_TYPE_BRIGHTNESS_APPLET		(cpm_brightness_applet_get_type ())
#define GPM_BRIGHTNESS_APPLET(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPM_TYPE_BRIGHTNESS_APPLET, GpmBrightnessApplet))
#define GPM_BRIGHTNESS_APPLET_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), GPM_TYPE_BRIGHTNESS_APPLET, GpmBrightnessAppletClass))
#define GPM_IS_BRIGHTNESS_APPLET(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPM_TYPE_BRIGHTNESS_APPLET))
#define GPM_IS_BRIGHTNESS_APPLET_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPM_TYPE_BRIGHTNESS_APPLET))
#define GPM_BRIGHTNESS_APPLET_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPM_TYPE_BRIGHTNESS_APPLET, GpmBrightnessAppletClass))

typedef struct{
	CafePanelApplet parent;
	/* applet state */
	gboolean call_worked; /* g-p-m refusing action */
	gboolean popped; /* the popup is shown */
	/* the popup and its widgets */
	CtkWidget *popup, *slider, *btn_plus, *btn_minus;
	/* the icon and a cache for size*/
	GdkPixbuf *icon;
	gint icon_width, icon_height;
	/* connection to g-p-m */
	DBusGProxy *proxy;
	DBusGConnection *connection;
	guint bus_watch_id;
	guint level;
	/* a cache for panel size */
	gint size;
} GpmBrightnessApplet;

typedef struct{
	CafePanelAppletClass	parent_class;
} GpmBrightnessAppletClass;

GType                cpm_brightness_applet_get_type  (void);


#define	GPM_DBUS_SERVICE		"org.cafe.PowerManager"
#define	GPM_DBUS_PATH_BACKLIGHT		"/org/cafe/PowerManager/Backlight"
#define	GPM_DBUS_INTERFACE_BACKLIGHT	"org.cafe.PowerManager.Backlight"

G_DEFINE_TYPE (GpmBrightnessApplet, cpm_brightness_applet, PANEL_TYPE_APPLET)

static void      cpm_applet_get_icon              (GpmBrightnessApplet *applet);
static void      cpm_applet_check_size            (GpmBrightnessApplet *applet);
static gboolean  cpm_applet_draw_cb               (GpmBrightnessApplet *applet);
static void      cpm_applet_change_background_cb  (GpmBrightnessApplet *applet,
						   CafePanelAppletBackgroundType arg1,
						   cairo_pattern_t *arg2, gpointer data);
static void      cpm_applet_theme_change_cb (CtkIconTheme *icon_theme, gpointer data);
static void      cpm_applet_stop_scroll_events_cb (CtkWidget *widget, CdkEvent  *event);
static gboolean  cpm_applet_destroy_popup_cb      (GpmBrightnessApplet *applet);
static void      cpm_applet_update_tooltip        (GpmBrightnessApplet *applet);
static void      cpm_applet_update_popup_level    (GpmBrightnessApplet *applet);
static gboolean  cpm_applet_plus_cb               (CtkWidget *w, GpmBrightnessApplet *applet);
static gboolean  cpm_applet_minus_cb              (CtkWidget *w, GpmBrightnessApplet *applet);
static gboolean  cpm_applet_key_press_cb          (CtkWidget *popup, CdkEventKey *event, GpmBrightnessApplet *applet);
static gboolean  cpm_applet_scroll_cb             (GpmBrightnessApplet *applet, CdkEventScroll *event);
static gboolean  cpm_applet_slide_cb              (CtkWidget *w, GpmBrightnessApplet *applet);
static void      cpm_applet_create_popup          (GpmBrightnessApplet *applet);
static gboolean  cpm_applet_popup_cb              (GpmBrightnessApplet *applet, CdkEventButton *event);
static void      cpm_applet_dialog_about_cb       (CtkAction *action, gpointer data);
static gboolean  cpm_applet_cb                    (CafePanelApplet *_applet, const gchar *iid, gpointer data);
static void      cpm_applet_destroy_cb            (CtkWidget *widget);

#define GPM_BRIGHTNESS_APPLET_ID		"BrightnessApplet"
#define GPM_BRIGHTNESS_APPLET_FACTORY_ID	"BrightnessAppletFactory"
#define GPM_BRIGHTNESS_APPLET_ICON		"cafe-brightness-applet"
#define GPM_BRIGHTNESS_APPLET_ICON_DISABLED	"cpm-brightness-lcd-disabled"
#define GPM_BRIGHTNESS_APPLET_ICON_INVALID	"cpm-brightness-lcd-invalid"
#define GPM_BRIGHTNESS_APPLET_NAME		_("Power Manager Brightness Applet")
#define GPM_BRIGHTNESS_APPLET_DESC		_("Adjusts laptop panel brightness.")
#define CAFE_PANEL_APPLET_VERTICAL(p)					\
	 (((p) == CAFE_PANEL_APPLET_ORIENT_LEFT) || ((p) == CAFE_PANEL_APPLET_ORIENT_RIGHT))

/**
 * cpm_applet_get_brightness:
 * Return value: Success value, or zero for failure
 **/
static gboolean
cpm_applet_get_brightness (GpmBrightnessApplet *applet)
{
	GError  *error = NULL;
	gboolean ret;
	guint policy_brightness;

	if (applet->proxy == NULL) {
		egg_warning ("not connected\n");
		return FALSE;
	}

	ret = dbus_g_proxy_call (applet->proxy, "GetBrightness", &error,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &policy_brightness,
				 G_TYPE_INVALID);
	if (error) {
		egg_debug ("ERROR: %s\n", error->message);
		g_error_free (error);
	}
	if (ret) {
		applet->level = policy_brightness;
	} else {
		/* abort as the DBUS method failed */
		egg_warning ("GetBrightness failed!\n");
	}

	return ret;
}

/**
 * cpm_applet_set_brightness:
 * Return value: Success value, or zero for failure
 **/
static gboolean
cpm_applet_set_brightness (GpmBrightnessApplet *applet)
{
	GError  *error = NULL;
	gboolean ret;

	if (applet->proxy == NULL) {
		egg_warning ("not connected");
		return FALSE;
	}

	ret = dbus_g_proxy_call (applet->proxy, "SetBrightness", &error,
				 G_TYPE_UINT, applet->level,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error) {
		egg_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (!ret) {
		/* abort as the DBUS method failed */
		egg_warning ("SetBrightness failed!");
	}

	return ret;
}

/**
 * cpm_applet_get_icon:
 * @applet: Brightness applet instance
 *
 * retrieve an icon from stock with a size adapted to panel
 **/
static void
cpm_applet_get_icon (GpmBrightnessApplet *applet)
{
	const gchar *icon;

	/* free */
	if (applet->icon != NULL) {
		g_object_unref (applet->icon);
		applet->icon = NULL;
	}

	if (applet->size <= 2) {
		return;
	}

	/* get icon */
	if (applet->proxy == NULL) {
		icon = GPM_BRIGHTNESS_APPLET_ICON_INVALID;
	} else if (applet->call_worked == FALSE) {
		icon = GPM_BRIGHTNESS_APPLET_ICON_DISABLED;
	} else {
		icon = GPM_BRIGHTNESS_APPLET_ICON;
	}

	applet->icon = ctk_icon_theme_load_icon (ctk_icon_theme_get_default (),
						 icon, applet->size - 2, 0, NULL);

	if (applet->icon == NULL) {
		egg_warning ("Cannot find %s!\n", icon);
	} else {
		egg_debug ("got icon %s!\n", icon);
		/* update size cache */
		applet->icon_height = gdk_pixbuf_get_height (applet->icon);
		applet->icon_width = gdk_pixbuf_get_width (applet->icon);
	}
}

/**
 * cpm_applet_check_size:
 * @applet: Brightness applet instance
 *
 * check if panel size has changed and applet adapt size
 **/
static void
cpm_applet_check_size (GpmBrightnessApplet *applet)
{
	CtkAllocation allocation;

	/* we don't use the size function here, but the yet allocated size because the
	   size value is false (kind of rounded) */
	ctk_widget_get_allocation (CTK_WIDGET (applet), &allocation);
	if (CAFE_PANEL_APPLET_VERTICAL(cafe_panel_applet_get_orient (CAFE_PANEL_APPLET (applet)))) {
		if (applet->size != allocation.width) {
			applet->size = allocation.width;
			cpm_applet_get_icon (applet);
			ctk_widget_set_size_request (CTK_WIDGET(applet), applet->size, applet->icon_height + 2);
		}
	} else {
		if (applet->size != allocation.height) {
			applet->size = allocation.height;
			cpm_applet_get_icon (applet);
			ctk_widget_set_size_request (CTK_WIDGET(applet), applet->icon_width + 2, applet->size);
		}
	}
}

/**
 * cpm_applet_draw_cb:
 * @applet: Brightness applet instance
 *
 * draws applet content (background + icon)
 **/
static gboolean
cpm_applet_draw_cb (GpmBrightnessApplet *applet)
{
	gint w, h, bg_type;
	CdkRGBA color;
	cairo_t *cr;
	cairo_pattern_t *pattern;
	CtkStyleContext *context;
	CtkAllocation allocation;

	if (ctk_widget_get_window (CTK_WIDGET(applet)) == NULL) {
		return FALSE;
	}

	/* retrieve applet size */
	cpm_applet_get_icon (applet);
	cpm_applet_check_size (applet);
	if (applet->size <= 2) {
		return FALSE;
	}

	/* if no icon, then don't try to display */
	if (applet->icon == NULL) {
		return FALSE;
	}

	ctk_widget_get_allocation (CTK_WIDGET (applet), &allocation);
	w = allocation.width;
	h = allocation.height;

	cr = cdk_cairo_create (ctk_widget_get_window (CTK_WIDGET(applet)));

	/* draw pixmap background */
	bg_type = cafe_panel_applet_get_background (CAFE_PANEL_APPLET (applet), &color, &pattern);
	if (bg_type == PANEL_PIXMAP_BACKGROUND && !applet->popped) {
		/* fill with given background pixmap */
		cairo_set_source (cr, pattern);
		cairo_rectangle (cr, 0, 0, w, h);
		cairo_fill (cr);
	}
	
	/* draw color background */
	if (bg_type == PANEL_COLOR_BACKGROUND && !applet->popped) {
		cdk_cairo_set_source_rgba (cr, &color);
		cairo_rectangle (cr, 0, 0, w, h);
		cairo_fill (cr);
	}

	/* fill with selected color if popped */
	if (applet->popped) {
		context = ctk_widget_get_style_context (CTK_WIDGET(applet));
		ctk_style_context_get_color (context, CTK_STATE_FLAG_SELECTED, &color);
		cdk_cairo_set_source_rgba (cr, &color);
		cairo_rectangle (cr, 0, 0, w, h);
		cairo_fill (cr);
	}

	/* draw icon at center */
	cdk_cairo_set_source_pixbuf (cr, applet->icon, (w - applet->icon_width)/2, (h - applet->icon_height)/2);
	cairo_paint (cr);

	cairo_destroy (cr);

	return TRUE;
}

/**
 * cpm_applet_change_background_cb:
 *
 * Enqueues an expose event (don't know why it's not the default behaviour)
 **/
static void
cpm_applet_change_background_cb (GpmBrightnessApplet *applet,
				 CafePanelAppletBackgroundType arg1,
				 cairo_pattern_t *arg2, gpointer data)
{
	ctk_widget_queue_draw (CTK_WIDGET (applet));
}

/**
 * cpm_applet_destroy_popup_cb:
 * @applet: Brightness applet instance
 *
 * destroys the popup (called if orientation has changed)
 **/
static gboolean
cpm_applet_destroy_popup_cb (GpmBrightnessApplet *applet)
{
	if (applet->popup != NULL) {
		ctk_widget_destroy (applet->popup);
		applet->popup = NULL;
		applet->popped = FALSE;
		cpm_applet_update_tooltip (applet);
	}
	return TRUE;
}

/**
 * cpm_applet_update_tooltip:
 * @applet: Brightness applet instance
 *
 * sets tooltip's content (percentage or disabled)
 **/
static void
cpm_applet_update_tooltip (GpmBrightnessApplet *applet)
{
	gchar *buf = NULL;
	if (applet->popped == FALSE) {
		if (applet->proxy == NULL) {
			buf = g_strdup (_("Cannot connect to cafe-power-manager"));
		} else if (applet->call_worked == FALSE) {
			buf = g_strdup (_("Cannot get laptop panel brightness"));
		} else {
			buf = g_strdup_printf (_("LCD brightness : %d%%"), applet->level);
		}
		ctk_widget_set_tooltip_text (CTK_WIDGET(applet), buf);
	} else {
		ctk_widget_set_tooltip_text (CTK_WIDGET(applet), NULL);
	}
	g_free (buf);
}

/**
 * cpm_applet_update_popup_level:
 * @applet: Brightness applet instance
 * @get_hw: set UI value from HW value
 * @set_hw: set HW value from UI value
 *
 * updates popup and hardware level of brightness
 * FALSE FAlSE -> set UI from cached value
 * TRUE  FAlSE -> set UI from HW value
 * TRUE  FALSE -> set HW from UI value, then set UI from HW value
 * FALSE TRUE  -> set HW from UI value
 **/
static void
cpm_applet_update_popup_level (GpmBrightnessApplet *applet)
{
	if (applet->popup != NULL) {
		ctk_widget_set_sensitive (applet->btn_plus, applet->level < 100);
		ctk_widget_set_sensitive (applet->btn_minus, applet->level > 0);
		ctk_range_set_value (CTK_RANGE(applet->slider), (guint) applet->level);
	}
	cpm_applet_update_tooltip (applet);
}

/**
 * cpm_applet_plus_cb:
 * @widget: The sending widget (plus button)
 * @applet: Brightness applet instance
 *
 * callback for the plus button
 **/
static gboolean
cpm_applet_plus_cb (CtkWidget *w, GpmBrightnessApplet *applet)
{
	if (applet->level < 100) {
		applet->level++;
	}
	applet->call_worked = cpm_applet_set_brightness (applet);
	cpm_applet_update_popup_level (applet);
	return TRUE;
}

/**
 * cpm_applet_minus_cb:
 * @widget: The sending widget (minus button)
 * @applet: Brightness applet instance
 *
 * callback for the minus button
 **/
static gboolean
cpm_applet_minus_cb (CtkWidget *w, GpmBrightnessApplet *applet)
{
	if (applet->level > 0) {
		applet->level--;
	}
	applet->call_worked = cpm_applet_set_brightness (applet);
	cpm_applet_update_popup_level (applet);
	return TRUE;
}

/**
 * cpm_applet_slide_cb:
 * @widget: The sending widget (slider)
 * @applet: Brightness applet instance
 *
 * callback for the slider
 **/
static gboolean
cpm_applet_slide_cb (CtkWidget *w, GpmBrightnessApplet *applet)
{
	applet->level = ctk_range_get_value (CTK_RANGE(applet->slider));
	applet->call_worked = cpm_applet_set_brightness (applet);
	cpm_applet_update_popup_level (applet);
	return TRUE;
}

/**
 * cpm_applet_slide_cb:
 * @applet: Brightness applet instance
 * @event: The key press event
 *
 * callback handling keyboard
 * mainly escape to unpop and arrows to change brightness
 **/
static gboolean
cpm_applet_key_press_cb (CtkWidget *popup, CdkEventKey *event, GpmBrightnessApplet *applet)
{
	int i;
	
	switch (event->keyval) {
	case CDK_KEY_KP_Enter:
	case CDK_KEY_ISO_Enter:
	case CDK_KEY_3270_Enter:
	case CDK_KEY_Return:
	case CDK_KEY_space:
	case CDK_KEY_KP_Space:
	case CDK_KEY_Escape:
		/* if yet popped, hide */
		if (applet->popped) {
			ctk_widget_hide (applet->popup);
			applet->popped = FALSE;
			cpm_applet_update_tooltip (applet);
			return TRUE;
		} else {
			return FALSE;
		}
		break;
	case CDK_KEY_Page_Up:
		for (i = 0;i < 10;i++) {
			cpm_applet_plus_cb (NULL, applet);
		}
		return TRUE;
		break;
	case CDK_KEY_Left:
	case CDK_KEY_Up:
		cpm_applet_plus_cb (NULL, applet);
		return TRUE;
		break;
	case CDK_KEY_Page_Down:
		for (i = 0;i < 10;i++) {
			cpm_applet_minus_cb (NULL, applet);
		}
		return TRUE;
		break;
	case CDK_KEY_Right:
	case CDK_KEY_Down:
		cpm_applet_minus_cb (NULL, applet);
		return TRUE;
		break;
	default:
		return FALSE;
		break;
	}

	return FALSE;
}

/**
 * cpm_applet_scroll_cb:
 * @applet: Brightness applet instance
 * @event: The scroll event
 *
 * callback handling mouse scrolls, either when the applet
 * is not popped and the mouse is over the applet, or when
 * the applet is popped and no matter where the mouse is.
 **/
static gboolean
cpm_applet_scroll_cb (GpmBrightnessApplet *applet, CdkEventScroll *event)
{
	int i;

	if (event->type == CDK_SCROLL) {
		if (event->direction == CDK_SCROLL_UP) {
			for (i = 0;i < 5;i++) {
				cpm_applet_plus_cb (NULL, applet);
			}
			
		} else {
			for (i = 0;i < 5;i++) {
				cpm_applet_minus_cb (NULL, applet);
			}
		}
		return TRUE;
	}

	return FALSE;
}

/**
 * on_popup_button_press:
 * @applet: Brightness applet instance
 * @event: The button press event
 *
 * hide popup on focus loss.
 **/
static gboolean
on_popup_button_press (CtkWidget      *widget,
                       CdkEventButton *event,
                       GpmBrightnessApplet *applet)
{
	CtkWidget *event_widget;

	if (event->type != CDK_BUTTON_PRESS) {
		return FALSE;
	}
	event_widget = ctk_get_event_widget ((CdkEvent *)event);
	g_debug ("Button press: %p dock=%p", event_widget, widget);
	if (event_widget == widget) {
		ctk_widget_hide (applet->popup);
		applet->popped = FALSE;
		cpm_applet_update_tooltip (applet);
		return TRUE;
	}

	return FALSE;
}

/**
 * cpm_applet_create_popup:
 * @applet: Brightness applet instance
 *
 * cretes a new popup according to orientation of panel
 **/
static void
cpm_applet_create_popup (GpmBrightnessApplet *applet)
{
	static CtkWidget *box, *frame;
	gint orientation = cafe_panel_applet_get_orient (CAFE_PANEL_APPLET (CAFE_PANEL_APPLET (applet)));

	cpm_applet_destroy_popup_cb (applet);

	/* slider */
	if (CAFE_PANEL_APPLET_VERTICAL(orientation)) {
		applet->slider = ctk_scale_new_with_range (CTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
		ctk_widget_set_size_request (applet->slider, 100, -1);
	} else {
		applet->slider = ctk_scale_new_with_range (CTK_ORIENTATION_VERTICAL, 0, 100, 1);
		ctk_widget_set_size_request (applet->slider, -1, 100);
	}
	ctk_range_set_inverted (CTK_RANGE(applet->slider), TRUE);
	ctk_scale_set_draw_value (CTK_SCALE(applet->slider), FALSE);
	ctk_range_set_value (CTK_RANGE(applet->slider), applet->level);
	g_signal_connect (G_OBJECT(applet->slider), "value-changed", G_CALLBACK(cpm_applet_slide_cb), applet);

	/* minus button */
	applet->btn_minus = ctk_button_new_with_label ("\342\210\222"); /* U+2212 MINUS SIGN */
	ctk_button_set_relief (CTK_BUTTON(applet->btn_minus), CTK_RELIEF_NONE);
	ctk_widget_set_can_focus (applet->btn_minus, FALSE);
	g_signal_connect (G_OBJECT(applet->btn_minus), "pressed", G_CALLBACK(cpm_applet_minus_cb), applet);

	/* plus button */
	applet->btn_plus = ctk_button_new_with_label ("+");
	ctk_button_set_relief (CTK_BUTTON(applet->btn_plus), CTK_RELIEF_NONE);
	ctk_widget_set_can_focus (applet->btn_plus, FALSE);
	g_signal_connect (G_OBJECT(applet->btn_plus), "pressed", G_CALLBACK(cpm_applet_plus_cb), applet);

	/* box */
	if (CAFE_PANEL_APPLET_VERTICAL(orientation)) {
		box = ctk_box_new (CTK_ORIENTATION_HORIZONTAL, 1);
	} else {
		box = ctk_box_new (CTK_ORIENTATION_VERTICAL, 1);
	}
	ctk_box_pack_start (CTK_BOX(box), applet->btn_plus, FALSE, FALSE, 0);
	ctk_box_pack_start (CTK_BOX(box), applet->slider, TRUE, TRUE, 0);
	ctk_box_pack_start (CTK_BOX(box), applet->btn_minus, FALSE, FALSE, 0);

	/* frame */
	frame = ctk_frame_new (NULL);
	ctk_frame_set_shadow_type (CTK_FRAME(frame), CTK_SHADOW_OUT);
	ctk_container_add (CTK_CONTAINER(frame), box);

	/* window */
	applet->popup = ctk_window_new (CTK_WINDOW_POPUP);
	ctk_window_set_type_hint (CTK_WINDOW(applet->popup), CDK_WINDOW_TYPE_HINT_UTILITY);
	ctk_container_add (CTK_CONTAINER(applet->popup), frame);

	/* window events */
	g_signal_connect (G_OBJECT(applet->popup), "button-press-event",
	                  G_CALLBACK (on_popup_button_press), applet);

	g_signal_connect (G_OBJECT(applet->popup), "key-press-event",
	                  G_CALLBACK(cpm_applet_key_press_cb), applet);

	/* Set volume control frame, slider and toplevel window to follow panel volume control theme */
	CtkWidget *toplevel = ctk_widget_get_toplevel (frame);
	CtkStyleContext *context;
	context = ctk_widget_get_style_context (CTK_WIDGET(toplevel));
	ctk_style_context_add_class(context,"cafe-panel-applet-slider");
	/*Make transparency possible in ctk3 theme3 */
 	CdkScreen *screen = ctk_widget_get_screen(CTK_WIDGET(toplevel));
	CdkVisual *visual = cdk_screen_get_rgba_visual(screen);
	ctk_widget_set_visual(CTK_WIDGET(toplevel), visual);
}

/**
 * cpm_applet_popup_cb:
 * @applet: Brightness applet instance
 *
 * pops and unpops
 **/
static gboolean
cpm_applet_popup_cb (GpmBrightnessApplet *applet, CdkEventButton *event)
{
	CtkAllocation allocation, popup_allocation;
	gint orientation, x, y;
	CdkWindow *window;
	CdkDisplay *display;
	CdkSeat *seat;

	/* react only to left mouse button */
	if (event->button != 1) {
		return FALSE;
	}

	/* if yet popped, release focus and hide */
	if (applet->popped) {
		ctk_widget_hide (applet->popup);
		applet->popped = FALSE;
		cpm_applet_update_tooltip (applet);
		return TRUE;
	}

	/* don't show the popup if brightness is unavailable */
	if (applet->level == -1) {
		return FALSE;
	}

	/* otherwise pop */
	applet->popped = TRUE;

	/* create a new popup (initial or if panel parameters changed) */
	if (applet->popup == NULL) {
		cpm_applet_create_popup (applet);
	}

	/* update UI for current brightness */
	cpm_applet_update_popup_level (applet);

	ctk_widget_show_all (applet->popup);

	/* retrieve geometry parameters and move window appropriately */
	orientation = cafe_panel_applet_get_orient (CAFE_PANEL_APPLET (CAFE_PANEL_APPLET (applet)));
	cdk_window_get_origin (ctk_widget_get_window (CTK_WIDGET(applet)), &x, &y);

	ctk_widget_get_allocation (CTK_WIDGET (applet), &allocation);
	ctk_widget_get_allocation (CTK_WIDGET (applet->popup), &popup_allocation);
	switch (orientation) {
	case CAFE_PANEL_APPLET_ORIENT_DOWN:
		x += allocation.x + allocation.width/2;
		y += allocation.y + allocation.height;
		x -= popup_allocation.width/2;
		break;
	case CAFE_PANEL_APPLET_ORIENT_UP:
		x += allocation.x + allocation.width/2;
		y += allocation.y;
		x -= popup_allocation.width/2;
		y -= popup_allocation.height;
		break;
	case CAFE_PANEL_APPLET_ORIENT_RIGHT:
		y += allocation.y + allocation.height/2;
		x += allocation.x + allocation.width;
		y -= popup_allocation.height/2;
		break;
	case CAFE_PANEL_APPLET_ORIENT_LEFT:
		y += allocation.y + allocation.height/2;
		x += allocation.x;
		x -= popup_allocation.width;
		y -= popup_allocation.height/2;
		break;
	default:
		g_assert_not_reached ();
	}

	ctk_window_move (CTK_WINDOW (applet->popup), x, y);

	/* grab input */
	window = ctk_widget_get_window (CTK_WIDGET (applet->popup));
	display = cdk_window_get_display (window);
	seat = cdk_display_get_default_seat (display);
	cdk_seat_grab (seat,
	               window,
	               CDK_SEAT_CAPABILITY_ALL,
	               TRUE,
	               NULL,
	               NULL,
	               NULL,
	               NULL);

	return TRUE;
}

/**
 * cpm_applet_theme_change_cb:
 *
 * Updtes icon when theme changes
 **/
static void
cpm_applet_theme_change_cb (CtkIconTheme *icon_theme, gpointer data)
{
	GpmBrightnessApplet *applet = GPM_BRIGHTNESS_APPLET (data);
	cpm_applet_get_icon (applet);
}

/**
 * cpm_applet_stop_scroll_events_cb:
 *
 * Prevents scroll events from reaching the tooltip
 **/
static void
cpm_applet_stop_scroll_events_cb (CtkWidget *widget, CdkEvent  *event)
{
	if (event->type == CDK_SCROLL)
		g_signal_stop_emission_by_name (widget, "event-after");
}

/**
 * cpm_applet_dialog_about_cb:
 *
 * displays about dialog
 **/
static void
cpm_applet_dialog_about_cb (CtkAction *action, gpointer data)
{
	static const gchar *authors[] = {
		"Benjamin Canou <bookeldor@gmail.com>",
		"Richard Hughes <richard@hughsie.com>",
		NULL
	};

	const char *documenters [] = {
		NULL
	};

	const char *license[] = {
		 N_("Power Manager is free software; you can redistribute it and/or "
		   "modify it under the terms of the GNU General Public License "
		   "as published by the Free Software Foundation; either version 2 "
		   "of the License, or (at your option) any later version."),

		 N_("Power Manager is distributed in the hope that it will be useful, "
		   "but WITHOUT ANY WARRANTY; without even the implied warranty of "
		   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
		   "GNU General Public License for more details."),

		 N_("You should have received a copy of the GNU General Public License "
		   "along with this program; if not, write to the Free Software "
		   "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA "
		   "02110-1301, USA.")
	};

	char *license_trans;

	license_trans = g_strjoin("\n\n", _(license[0]), _(license[1]), _(license[2]), NULL);

	ctk_show_about_dialog (NULL,
	                       "program-name", GPM_BRIGHTNESS_APPLET_NAME,
	                       "version", VERSION,
	                       "title", _("About Power Manager Brightness Applet"),
	                       "comments", GPM_BRIGHTNESS_APPLET_DESC,
	                       "copyright", _("Copyright \xC2\xA9 2006 Benjamin Canou\n"
	                                      "Copyright \xC2\xA9 2011-2020 CAFE developers"),
	                       "icon-name", GPM_BRIGHTNESS_APPLET_ICON,
	                       "logo-icon-name", GPM_BRIGHTNESS_APPLET_ICON,
	                       "license", license_trans,
	                       "authors", authors,
	                       "documenters", documenters,
	                       "translator-credits", _("translator-credits"),
	                       "wrap-license", TRUE,
	                       "website", "https://cafe-desktop.org",
	                       NULL);

	g_free (license_trans);
}

/**
 * cpm_applet_help_cb:
 *
 * open gpm help
 **/
static void
cpm_applet_help_cb (CtkAction *action, gpointer data)
{
	cpm_help_display ("applets-general#applets-brightness");
}

/**
 * cpm_applet_destroy_cb:
 * @widget: Class instance to destroy
 **/
static void
cpm_applet_destroy_cb (CtkWidget *widget)
{
	GpmBrightnessApplet *applet = GPM_BRIGHTNESS_APPLET(widget);

	g_bus_unwatch_name (applet->bus_watch_id);
	if (applet->icon != NULL)
		g_object_unref (applet->icon);
}

/**
 * cpm_brightness_applet_class_init:
 * @klass: Class instance
 **/
static void
cpm_brightness_applet_class_init (GpmBrightnessAppletClass *class)
{
	/* nothing to do here */
}

static void
brightness_changed_cb (DBusGProxy          *proxy,
		       guint	            brightness,
		       GpmBrightnessApplet *applet)
{
	egg_debug ("BrightnessChanged detected: %i\n", brightness);
	applet->level = brightness;
}

/**
 * cpm_brightness_applet_dbus_connect:
 **/
gboolean
cpm_brightness_applet_dbus_connect (GpmBrightnessApplet *applet)
{
	GError *error = NULL;

	if (applet->connection == NULL) {
		egg_debug ("get connection\n");
		g_clear_error (&error);
		applet->connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
		if (error != NULL) {
			egg_warning ("Could not connect to DBUS daemon: %s", error->message);
			g_error_free (error);
			applet->connection = NULL;
			return FALSE;
		}
	}
	if (applet->proxy == NULL) {
		egg_debug ("get proxy\n");
		g_clear_error (&error);
		applet->proxy = dbus_g_proxy_new_for_name_owner (applet->connection,
							 GPM_DBUS_SERVICE,
							 GPM_DBUS_PATH_BACKLIGHT,
							 GPM_DBUS_INTERFACE_BACKLIGHT,
							 &error);
		if (error != NULL) {
			egg_warning ("Cannot connect, maybe the daemon is not running: %s\n", error->message);
			g_error_free (error);
			applet->proxy = NULL;
			return FALSE;
		}
		dbus_g_proxy_add_signal (applet->proxy, "BrightnessChanged",
					 G_TYPE_UINT, G_TYPE_INVALID);
		dbus_g_proxy_connect_signal (applet->proxy, "BrightnessChanged",
					     G_CALLBACK (brightness_changed_cb),
					     applet, NULL);
		/* reset, we might be starting race */
		applet->call_worked = cpm_applet_get_brightness (applet);
	}
	return TRUE;
}

/**
 * cpm_brightness_applet_dbus_disconnect:
 **/
gboolean
cpm_brightness_applet_dbus_disconnect (GpmBrightnessApplet *applet)
{
	if (applet->proxy != NULL) {
		egg_debug ("removing proxy\n");
		g_object_unref (applet->proxy);
		applet->proxy = NULL;
	}
	return TRUE;
}

/**
 * cpm_brightness_applet_name_appeared_cb:
 **/
static void
cpm_brightness_applet_name_appeared_cb (GDBusConnection *connection,
					const gchar *name,
					const gchar *name_owner,
					GpmBrightnessApplet *applet)
{
	cpm_brightness_applet_dbus_connect (applet);
	cpm_applet_update_tooltip (applet);
	cpm_applet_draw_cb (applet);
}

/**
 * cpm_brightness_applet_name_vanished_cb:
 **/
void
cpm_brightness_applet_name_vanished_cb (GDBusConnection *connection,
					 const gchar *name,
					 GpmBrightnessApplet *applet)
{
	cpm_brightness_applet_dbus_disconnect (applet);
	cpm_applet_update_tooltip (applet);
	cpm_applet_draw_cb (applet);
}

/**
 * cpm_brightness_applet_init:
 * @applet: Brightness applet instance
 **/
static void
cpm_brightness_applet_init (GpmBrightnessApplet *applet)
{
	DBusGConnection *connection;

	/* initialize fields */
	applet->size = 0;
	applet->call_worked = TRUE;
	applet->popped = FALSE;
	applet->popup = NULL;
	applet->icon = NULL;
	applet->connection = NULL;
	applet->proxy = NULL;

	/* Add application specific icons to search path */
	ctk_icon_theme_append_search_path (ctk_icon_theme_get_default (),
	                                   GPM_ICONS_DATA);

	/* monitor the daemon */
	applet->bus_watch_id =
		g_bus_watch_name (G_BUS_TYPE_SESSION,
				  GPM_DBUS_SERVICE,
				  G_BUS_NAME_WATCHER_FLAGS_NONE,
				  (GBusNameAppearedCallback) cpm_brightness_applet_name_appeared_cb,
				  (GBusNameVanishedCallback) cpm_brightness_applet_name_vanished_cb,
				  applet, NULL);

	/* coldplug */
	applet->call_worked = cpm_applet_get_brightness (applet);
	cpm_applet_update_popup_level (applet);

	/* prepare */
	cafe_panel_applet_set_flags (CAFE_PANEL_APPLET (applet), CAFE_PANEL_APPLET_EXPAND_MINOR);
	ctk_widget_set_events (CTK_WIDGET (applet), CDK_SCROLL_MASK);

	/* show */
	ctk_widget_show_all (CTK_WIDGET(applet));

	/* set appropriate size and load icon accordingly */
	cpm_applet_draw_cb (applet);

	/* connect */
	g_signal_connect (G_OBJECT(applet), "button-press-event",
			  G_CALLBACK(cpm_applet_popup_cb), NULL);

	g_signal_connect (G_OBJECT(applet), "scroll-event",
			  G_CALLBACK(cpm_applet_scroll_cb), NULL);

	/* We use g_signal_connect_after because letting the panel draw
	 * the background is the only way to have the correct
	 * background when a theme defines a background picture. */
	g_signal_connect_after (G_OBJECT(applet), "draw",
				G_CALLBACK(cpm_applet_draw_cb), NULL);

	g_signal_connect (G_OBJECT(applet), "change-background",
			  G_CALLBACK(cpm_applet_change_background_cb), NULL);

	g_signal_connect (G_OBJECT(applet), "change-orient",
			  G_CALLBACK(cpm_applet_draw_cb), NULL);

	g_signal_connect (G_OBJECT(applet), "change-orient",
			  G_CALLBACK(cpm_applet_destroy_popup_cb), NULL);

	g_signal_connect (G_OBJECT(applet), "destroy",
			  G_CALLBACK(cpm_applet_destroy_cb), NULL);

	/* prevent scroll events from reaching the tooltip */
	g_signal_connect (G_OBJECT (applet), "event-after", G_CALLBACK (cpm_applet_stop_scroll_events_cb), NULL);

	g_signal_connect (ctk_icon_theme_get_default (), "changed", G_CALLBACK (cpm_applet_theme_change_cb), applet);
}

/**
 * cpm_applet_cb:
 * @_applet: GpmBrightnessApplet instance created by the applet factory
 * @iid: Applet id
 *
 * the function called by libcafe-panel-applet factory after creation
 **/
static gboolean
cpm_applet_cb (CafePanelApplet *_applet, const gchar *iid, gpointer data)
{
	GpmBrightnessApplet *applet = GPM_BRIGHTNESS_APPLET(_applet);
	CtkActionGroup *action_group;
	gchar *ui_path;

	static const CtkActionEntry menu_actions [] = {
		{ "About", "help-about", N_("_About"),
		  NULL, NULL,
		  G_CALLBACK (cpm_applet_dialog_about_cb) },
		{ "Help", "help-browser", N_("_Help"),
		  NULL, NULL,
		  G_CALLBACK (cpm_applet_help_cb) }
	};

	if (strcmp (iid, GPM_BRIGHTNESS_APPLET_ID) != 0) {
		return FALSE;
	}

	action_group = ctk_action_group_new ("Brightness Applet Actions");
	ctk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);
	ctk_action_group_add_actions (action_group,
				      menu_actions,
				      G_N_ELEMENTS (menu_actions),
				      applet);
	ui_path = g_build_filename (BRIGHTNESS_MENU_UI_DIR, "brightness-applet-menu.xml", NULL);
	cafe_panel_applet_setup_menu_from_file (CAFE_PANEL_APPLET (applet), ui_path, action_group);
	g_free (ui_path);
	g_object_unref (action_group);

	cpm_applet_draw_cb (applet);
	return TRUE;
}

/**
 * this generates a main with a applet factory
 **/
CAFE_PANEL_APPLET_OUT_PROCESS_FACTORY
 (/* the factory iid */
 GPM_BRIGHTNESS_APPLET_FACTORY_ID,
 /* generates brighness applets instead of regular cafe applets  */
 GPM_TYPE_BRIGHTNESS_APPLET,
 /* the applet name */
 "BrightnessApplet",
 /* our callback (with no user data) */
 cpm_applet_cb, NULL);
