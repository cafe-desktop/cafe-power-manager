/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2010 Richard Hughes <richard@hughsie.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <cdk/cdkx.h>
#include <ctk/ctk.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include "egg-discrete.h"
#include "egg-debug.h"

#include "cpm-brightness.h"
#include "cpm-common.h"
#include "cpm-marshal.h"

#define CPM_SOLE_SETTER_USE_CACHE	TRUE	/* this may be insanity */

struct CpmBrightnessPrivate
{
	gboolean		 has_changed_events;
	gboolean		 cache_trusted;
	guint			 cache_percentage;
	guint			 last_set_hw;
	Atom			 backlight;
	Display			*dpy;
	CdkWindow		*root_window;
	guint			 shared_value;
	gboolean		 has_extension;
	gboolean		 hw_changed;
	/* A cache of XRRScreenResources is used as XRRGetScreenResources is expensive */
	GPtrArray		*resources;
	gint			 extension_levels;
	gint			 extension_current;
};

enum {
	BRIGHTNESS_CHANGED,
	LAST_SIGNAL
};

typedef enum {
	ACTION_BACKLIGHT_GET,
	ACTION_BACKLIGHT_SET,
	ACTION_BACKLIGHT_INC,
	ACTION_BACKLIGHT_DEC
} CpmXRandROp;

G_DEFINE_TYPE_WITH_PRIVATE (CpmBrightness, cpm_brightness, G_TYPE_OBJECT)

static guint signals [LAST_SIGNAL] = { 0 };
static gpointer cpm_brightness_object = NULL;

/**
 * cpm_brightness_helper_strtoint:
 * @text: The text to be converted
 * @value: The return numeric return value
 *
 * Convert a string to a signed integer value in a safe way.
 *
 * Return value: %TRUE if the string was converted correctly
 **/
static gboolean
cpm_brightness_helper_strtoint (const gchar *text, gint *value)
{
	gchar *endptr = NULL;
	gint64 value_raw;

	if (text == NULL)
		return FALSE;

	value_raw = g_ascii_strtoll (text, &endptr, 10);

	if (endptr == text)
		return FALSE;
	if (errno == ERANGE || value_raw > G_MAXINT || value_raw < G_MININT)
		return FALSE;

	*value = (gint) value_raw;
	return TRUE;
}

/**
 * cpm_brightness_helper_get_value:
 **/
static gint
cpm_brightness_helper_get_value (const gchar *argument)
{
	gboolean ret;
	GError *error = NULL;
	gchar *stdout_data = NULL;
	gint exit_status = 0;
	gint value = -1;
	gchar *command = NULL;

	/* get the data */
	command = g_strdup_printf (SBINDIR "/cafe-power-backlight-helper --%s", argument);
	ret = g_spawn_command_line_sync (command,
					 &stdout_data, NULL, &exit_status, &error);
	if (!ret) {
		egg_error ("failed to get value: %s", error->message);
		g_error_free (error);
		goto out;
	}
	egg_debug ("executing %s retval: %i", command, exit_status);

	/* parse for a number */
	ret = cpm_brightness_helper_strtoint (stdout_data, &value);
	if (!ret)
		goto out;
out:
	g_free (command);
	g_free (stdout_data);
	return value;
}

/**
 * cpm_brightness_helper_set_value:
 **/
static gboolean
cpm_brightness_helper_set_value (const gchar *argument, gint value)
{
	gboolean ret;
	GError *error = NULL;
	gint exit_status = 0;
	gchar *command = NULL;

	/* get the data */
	command = g_strdup_printf ("pkexec " SBINDIR "/cafe-power-backlight-helper --%s %i", argument, value);
	ret = g_spawn_command_line_sync (command, NULL, NULL, &exit_status, &error);
	if (!ret) {
		egg_error ("failed to get value: %s", error->message);
		g_error_free (error);
		goto out;
	}
	egg_debug ("executing %s retval: %i", command, exit_status);
out:
	g_free (command);
	return ret;
}

/**
 * cpm_brightness_get_step:
 * @levels: The number of levels supported
 * Return value: the amount of hardware steps to do on each increment or decrement
 **/
static guint
cpm_brightness_get_step (guint levels)
{
	/* macbook pro has a bazzillion brightness levels, do in 5% steps */
	if (levels > 20)
		return levels / 20;
	return 1;
}

/**
 * cpm_brightness_output_get_internal:
 **/
static gboolean
cpm_brightness_output_get_internal (CpmBrightness *brightness, RROutput output, guint *cur)
{
	unsigned long nitems;
	unsigned long bytes_after;
	guint *prop;
	Atom actual_type;
	int actual_format;
	gboolean ret = FALSE;

	g_return_val_if_fail (CPM_IS_BRIGHTNESS (brightness), FALSE);

	if (brightness->priv->backlight == None)
		return FALSE;

	if (XRRGetOutputProperty (brightness->priv->dpy, output, brightness->priv->backlight,
				  0, 4, False, False, None,
				  &actual_type, &actual_format,
				  &nitems, &bytes_after, ((unsigned char **)&prop)) != Success) {
		egg_debug ("failed to get property");
		return FALSE;
	}
	if (actual_type == XA_INTEGER && nitems == 1 && actual_format == 32) {
		memcpy (cur, prop, sizeof (guint));
		ret = TRUE;
	}
	XFree (prop);
	return ret;
}

/**
 * cpm_brightness_output_set_internal:
 **/
static gboolean
cpm_brightness_output_set_internal (CpmBrightness *brightness, RROutput output, guint value)
{
	CdkDisplay *display;

	gboolean ret = TRUE;

	g_return_val_if_fail (CPM_IS_BRIGHTNESS (brightness), FALSE);

	/* don't abort on error */
	display = cdk_display_get_default ();
	cdk_x11_display_error_trap_push (display);
	XRRChangeOutputProperty (brightness->priv->dpy, output, brightness->priv->backlight, XA_INTEGER, 32,
				 PropModeReplace, (unsigned char *) &value, 1);
	XFlush (brightness->priv->dpy);
	cdk_display_flush (display);
	if (cdk_x11_display_error_trap_pop (display)) {
		egg_warning ("failed to XRRChangeOutputProperty for brightness %i", value);
		ret = FALSE;
	}
	/* we changed the hardware */
	if (ret)
		brightness->priv->hw_changed = TRUE;
	return ret;
}

/**
 * cpm_brightness_setup_display:
 **/
static gboolean
cpm_brightness_setup_display (CpmBrightness *brightness)
{
	gint major, minor;

	g_return_val_if_fail (CPM_IS_BRIGHTNESS (brightness), FALSE);

	/* get the display */
	brightness->priv->dpy = CDK_DISPLAY_XDISPLAY (cdk_display_get_default());
	if (!brightness->priv->dpy) {
		egg_error ("Cannot open display");
		return FALSE;
	}
	/* is XRandR new enough? */
	if (!XRRQueryVersion (brightness->priv->dpy, &major, &minor)) {
		egg_debug ("RandR extension missing");
		return FALSE;
	}
	if (major < 1 || (major == 1 && minor < 3)) {
		egg_debug ("RandR version %d.%d too old", major, minor);
		return FALSE;
	}
	/* Can we support "Backlight" */
	brightness->priv->backlight = XInternAtom (brightness->priv->dpy, "Backlight", True);
	if (brightness->priv->backlight == None) {
		/* Do we support "BACKLIGHT" (legacy) */
		brightness->priv->backlight = XInternAtom (brightness->priv->dpy, "BACKLIGHT", True);
		if (brightness->priv->backlight == None) {
			egg_debug ("No outputs have backlight property");
			return FALSE;
		}
	}
	return TRUE;
}

/**
 * cpm_brightness_output_get_limits:
 **/
static gboolean
cpm_brightness_output_get_limits (CpmBrightness *brightness, RROutput output,
					 guint *min, guint *max)
{
	XRRPropertyInfo *info;
	gboolean ret = TRUE;

	g_return_val_if_fail (CPM_IS_BRIGHTNESS (brightness), FALSE);

	info = XRRQueryOutputProperty (brightness->priv->dpy, output, brightness->priv->backlight);
	if (info == NULL) {
		egg_debug ("could not get output property");
		return FALSE;
	}
	if (!info->range || info->num_values != 2) {
		egg_debug ("was not range");
		ret = FALSE;
		goto out;
	}
	*min = info->values[0];
	*max = info->values[1];
out:
	XFree (info);
	return ret;
}

/**
 * cpm_brightness_output_get_percentage:
 **/
static gboolean
cpm_brightness_output_get_percentage (CpmBrightness *brightness, RROutput output)
{
	guint cur;
	gboolean ret;
	guint min, max;
	guint percentage;

	g_return_val_if_fail (CPM_IS_BRIGHTNESS (brightness), FALSE);

	ret = cpm_brightness_output_get_internal (brightness, output, &cur);
	if (!ret)
		return FALSE;
	ret = cpm_brightness_output_get_limits (brightness, output, &min, &max);
	if (!ret || min == max)
		return FALSE;
	egg_debug ("hard value=%i, min=%i, max=%i", cur, min, max);
	percentage = egg_discrete_to_percent (cur, (max-min)+1);
	egg_debug ("percentage %i", percentage);
	brightness->priv->shared_value = percentage;
	return TRUE;
}

/**
 * cpm_brightness_output_down:
 **/
static gboolean
cpm_brightness_output_down (CpmBrightness *brightness, RROutput output)
{
	guint cur;
	guint step;
	gboolean ret;
	guint min, max;

	g_return_val_if_fail (CPM_IS_BRIGHTNESS (brightness), FALSE);

	ret = cpm_brightness_output_get_internal (brightness, output, &cur);
	if (!ret)
		return FALSE;
	ret = cpm_brightness_output_get_limits (brightness, output, &min, &max);
	if (!ret || min == max)
		return FALSE;
	egg_debug ("hard value=%i, min=%i, max=%i", cur, min, max);
	if (cur == min) {
		egg_debug ("already min");
		return TRUE;
	}
	step = cpm_brightness_get_step ((max-min)+1);
	if (cur < step) {
		egg_debug ("truncating to %i", min);
		cur = min;
	} else {
		cur -= step;
	}
	ret = cpm_brightness_output_set_internal (brightness, output, cur);
	return ret;
}

/**
 * cpm_brightness_output_up:
 **/
static gboolean
cpm_brightness_output_up (CpmBrightness *brightness, RROutput output)
{
	guint cur;
	gboolean ret;
	guint min, max;

	g_return_val_if_fail (CPM_IS_BRIGHTNESS (brightness), FALSE);

	ret = cpm_brightness_output_get_internal (brightness, output, &cur);
	if (!ret)
		return FALSE;
	ret = cpm_brightness_output_get_limits (brightness, output, &min, &max);
	if (!ret || min == max)
		return FALSE;
	egg_debug ("hard value=%i, min=%i, max=%i", cur, min, max);
	if (cur == max) {
		egg_debug ("already max");
		return TRUE;
	}
	cur += cpm_brightness_get_step ((max-min)+1);
	if (cur > max) {
		egg_debug ("truncating to %i", max);
		cur = max;
	}
	ret = cpm_brightness_output_set_internal (brightness, output, cur);
	return ret;
}

/**
 * cpm_brightness_output_set:
 **/
static gboolean
cpm_brightness_output_set (CpmBrightness *brightness, RROutput output)
{
	guint cur;
	gboolean ret;
	guint min, max;
	gint i;
	gint shared_value_abs;
	guint step;

	g_return_val_if_fail (CPM_IS_BRIGHTNESS (brightness), FALSE);

	ret = cpm_brightness_output_get_internal (brightness, output, &cur);
	if (!ret)
		return FALSE;
	ret = cpm_brightness_output_get_limits (brightness, output, &min, &max);
	if (!ret || min == max)
		return FALSE;

	shared_value_abs = egg_discrete_from_percent (brightness->priv->shared_value, (max-min)+1);
	egg_debug ("percent=%i, absolute=%i", brightness->priv->shared_value, shared_value_abs);

	egg_debug ("hard value=%i, min=%i, max=%i", cur, min, max);
	if (shared_value_abs > (gint) max)
		shared_value_abs = max;
	if (shared_value_abs < (gint) min)
		shared_value_abs = min;
	if ((gint) cur == shared_value_abs) {
		egg_debug ("already set %i", cur);
		return TRUE;
	}

	/* step the correct way */
	if ((gint) cur < shared_value_abs) {

		/* some adaptors have a large number of steps */
		step = cpm_brightness_get_step (shared_value_abs - cur);
		egg_debug ("using step of %i", step);

		/* going up */
		for (i=cur; i<=shared_value_abs; i+=step) {
			ret = cpm_brightness_output_set_internal (brightness, output, i);
			if (!ret)
				break;
			if ((gint) cur != shared_value_abs)
				g_usleep (1000 * CPM_BRIGHTNESS_DIM_INTERVAL);
		}
	} else {

		/* some adaptors have a large number of steps */
		step = cpm_brightness_get_step (cur - shared_value_abs);
		egg_debug ("using step of %i", step);

		/* going down */
		for (i=cur; i>=shared_value_abs; i-=step) {
			ret = cpm_brightness_output_set_internal (brightness, output, i);
			if (!ret)
				break;
			if ((gint) cur != shared_value_abs)
				g_usleep (1000 * CPM_BRIGHTNESS_DIM_INTERVAL);
		}
	}
	return TRUE;
}

/**
 * cpm_brightness_foreach_resource:
 **/
static gboolean
cpm_brightness_foreach_resource (CpmBrightness *brightness, CpmXRandROp op, XRRScreenResources *resources)
{
	gint i;
	gboolean ret;
	gboolean success_any = FALSE;
	RROutput output;

	g_return_val_if_fail (CPM_IS_BRIGHTNESS (brightness), FALSE);

	/* do for each output */
	for (i=0; i<resources->noutput; i++) {
		output = resources->outputs[i];
		egg_debug ("resource %i of %i", i+1, resources->noutput);
		if (op==ACTION_BACKLIGHT_GET) {
			ret = cpm_brightness_output_get_percentage (brightness, output);
		} else if (op==ACTION_BACKLIGHT_INC) {
			ret = cpm_brightness_output_up (brightness, output);
		} else if (op==ACTION_BACKLIGHT_DEC) {
			ret = cpm_brightness_output_down (brightness, output);
		} else if (op==ACTION_BACKLIGHT_SET) {
			ret = cpm_brightness_output_set (brightness, output);
		} else {
			ret = FALSE;
			egg_warning ("op not known");
		}
		if (ret) {
			success_any = TRUE;
		}
	}
	return success_any;
}

/**
 * cpm_brightness_foreach_screen:
 **/
static gboolean
cpm_brightness_foreach_screen (CpmBrightness *brightness, CpmXRandROp op)
{
	guint i;
	guint length;
	XRRScreenResources *resource;
	gboolean ret;
	gboolean success_any = FALSE;

	g_return_val_if_fail (CPM_IS_BRIGHTNESS (brightness), FALSE);

	/* Return immediately if we can't use XRandR */
	if (!brightness->priv->has_extension)
		return FALSE;

	/* do for each screen */
	length = brightness->priv->resources->len;
	for (i=0; i<length; i++) {
		resource = (XRRScreenResources *) g_ptr_array_index (brightness->priv->resources, i);
		egg_debug ("using resource %p", resource);
		ret = cpm_brightness_foreach_resource (brightness, op, resource);
		if (ret)
			success_any = TRUE;
	}
	XSync (brightness->priv->dpy, False);
	return success_any;
}

/**
 * cpm_brightness_trust_cache:
 * @brightness: This brightness class instance
 * Return value: if we can trust the cache
 **/
static gboolean
cpm_brightness_trust_cache (CpmBrightness *brightness)
{
	g_return_val_if_fail (CPM_IS_BRIGHTNESS (brightness), FALSE);
	/* only return the cached value if the cache is trusted and we have change events */
	if (brightness->priv->cache_trusted && brightness->priv->has_changed_events) {
		egg_debug ("using cache for value %u (okay)", brightness->priv->cache_percentage);
		return TRUE;
	}

	/* can we trust that if we set a value 5 minutes ago, will it still be valid now?
	 * if we have multiple things setting policy on the workstation, e.g. fast user switching
	 * or kpowersave, then this will be invalid -- this logic may be insane */
	if (CPM_SOLE_SETTER_USE_CACHE && brightness->priv->cache_trusted) {
		egg_warning ("using cache for value %u (probably okay)", brightness->priv->cache_percentage);
		return TRUE;
	}
	return FALSE;
}

/**
 * cpm_brightness_set:
 * @brightness: This brightness class instance
 * @percentage: The percentage brightness
 * @hw_changed: If the hardware was changed, i.e. the brightness changed
 * Return value: %TRUE if success, %FALSE if there was an error
 **/
gboolean
cpm_brightness_set (CpmBrightness *brightness, guint percentage, gboolean *hw_changed)
{
	gboolean ret = FALSE;
	gboolean trust_cache;

	g_return_val_if_fail (CPM_IS_BRIGHTNESS (brightness), FALSE);

	/* can we check the new value with the cache? */
	trust_cache = cpm_brightness_trust_cache (brightness);
	if (trust_cache && percentage == brightness->priv->cache_percentage) {
		egg_debug ("not setting the same value %i", percentage);
		return TRUE;
	}

	/* set the value we want */
	brightness->priv->shared_value = percentage;

	/* reset to not-changed */
	brightness->priv->hw_changed = FALSE;
	ret = cpm_brightness_foreach_screen (brightness, ACTION_BACKLIGHT_SET);

	/* legacy fallback */
	if (!ret) {
		if (brightness->priv->extension_levels < 0)
			brightness->priv->extension_levels = cpm_brightness_helper_get_value ("get-max-brightness");
		brightness->priv->extension_current = egg_discrete_from_percent (percentage, brightness->priv->extension_levels+1);
		ret = cpm_brightness_helper_set_value ("set-brightness", brightness->priv->extension_current);
	}

	/* did the hardware have to be modified? */
	if (ret && hw_changed != NULL)
		*hw_changed = brightness->priv->hw_changed;

	/* we did something to the hardware, so untrusted */
	if (ret)
		brightness->priv->cache_trusted = FALSE;

	return ret;
}

/**
 * cpm_brightness_get:
 * @brightness: This brightness class instance
 * Return value: The percentage brightness, or -1 for no hardware or error
 *
 * Gets the current (or at least what this class thinks is current) percentage
 * brightness. This is quick as no HAL inquiry is done.
 **/
gboolean
cpm_brightness_get (CpmBrightness *brightness, guint *percentage)
{
	gboolean ret = FALSE;
	gboolean trust_cache;
	guint percentage_local;

	g_return_val_if_fail (CPM_IS_BRIGHTNESS (brightness), FALSE);
	g_return_val_if_fail (percentage != NULL, FALSE);

	/* can we use the cache? */
	trust_cache = cpm_brightness_trust_cache (brightness);
	if (trust_cache) {
		*percentage = brightness->priv->cache_percentage;
		return TRUE;
	}

	/* get the brightness from hardware -- slow */
	ret = cpm_brightness_foreach_screen (brightness, ACTION_BACKLIGHT_GET);
	percentage_local = brightness->priv->shared_value;

	/* legacy fallback */
	if (!ret) {
		if (brightness->priv->extension_levels < 0)
			brightness->priv->extension_levels = cpm_brightness_helper_get_value ("get-max-brightness");
		brightness->priv->extension_current = cpm_brightness_helper_get_value ("get-brightness");
		percentage_local = egg_discrete_to_percent (brightness->priv->extension_current, brightness->priv->extension_levels+1);
		ret = TRUE;
	}

	/* valid? */
	if (percentage_local > 100) {
		egg_warning ("percentage value of %i will be truncated", percentage_local);
		percentage_local = 100;
	}

	/* a new value is always trusted if the method and checks succeed */
	if (ret) {
		brightness->priv->cache_percentage = percentage_local;
		brightness->priv->cache_trusted = TRUE;
		*percentage = percentage_local;
	} else {
		brightness->priv->cache_trusted = FALSE;
	}
	return ret;
}

/**
 * cpm_brightness_up:
 * @brightness: This brightness class instance
 * @hw_changed: If the hardware was changed, i.e. the brightness changed
 * Return value: %TRUE if success, %FALSE if there was an error
 *
 * If possible, put the brightness of the LCD up one unit.
 **/
gboolean
cpm_brightness_up (CpmBrightness *brightness, gboolean *hw_changed)
{
	gboolean ret = FALSE;
	guint step;

	g_return_val_if_fail (CPM_IS_BRIGHTNESS (brightness), FALSE);

	/* reset to not-changed */
	brightness->priv->hw_changed = FALSE;
	ret = cpm_brightness_foreach_screen (brightness, ACTION_BACKLIGHT_INC);

	/* did the hardware have to be modified? */
	if (ret && hw_changed != NULL)
		*hw_changed = brightness->priv->hw_changed;

	/* we did something to the hardware, so untrusted */
	if (ret)
		brightness->priv->cache_trusted = FALSE;

	/* legacy fallback */
	if (!ret) {
		if (brightness->priv->extension_levels < 0)
			brightness->priv->extension_levels = cpm_brightness_helper_get_value ("get-max-brightness");
		brightness->priv->extension_current = cpm_brightness_helper_get_value ("get-brightness");

		/* increase by the step, limiting to the maximum possible levels */
		if (brightness->priv->extension_current < brightness->priv->extension_levels) {
			step = cpm_brightness_get_step (brightness->priv->extension_levels);
			brightness->priv->extension_current += step;
			if (brightness->priv->extension_current > brightness->priv->extension_levels)
				brightness->priv->extension_current = brightness->priv->extension_levels;
			ret = cpm_brightness_helper_set_value ("set-brightness", brightness->priv->extension_current);
		}
		if (hw_changed != NULL)
			*hw_changed = ret;
		brightness->priv->cache_trusted = FALSE;
		goto out;
	}
out:
	return ret;
}

/**
 * cpm_brightness_down:
 * @brightness: This brightness class instance
 * @hw_changed: If the hardware was changed, i.e. the brightness changed
 * Return value: %TRUE if success, %FALSE if there was an error
 *
 * If possible, put the brightness of the LCD down one unit.
 **/
gboolean
cpm_brightness_down (CpmBrightness *brightness, gboolean *hw_changed)
{
	gboolean ret = FALSE;
	guint step;

	g_return_val_if_fail (CPM_IS_BRIGHTNESS (brightness), FALSE);

	/* reset to not-changed */
	brightness->priv->hw_changed = FALSE;
	ret = cpm_brightness_foreach_screen (brightness, ACTION_BACKLIGHT_DEC);

	/* did the hardware have to be modified? */
	if (ret && hw_changed != NULL)
		*hw_changed = brightness->priv->hw_changed;

	/* we did something to the hardware, so untrusted */
	if (ret)
		brightness->priv->cache_trusted = FALSE;

	/* legacy fallback */
	if (!ret) {
		if (brightness->priv->extension_levels < 0)
			brightness->priv->extension_levels = cpm_brightness_helper_get_value ("get-max-brightness");
		brightness->priv->extension_current = cpm_brightness_helper_get_value ("get-brightness");

		/* decrease by the step, limiting to zero */
		if (brightness->priv->extension_current > 0) {
			step = cpm_brightness_get_step (brightness->priv->extension_levels);
			brightness->priv->extension_current -= step;
			if (brightness->priv->extension_current < 0)
				brightness->priv->extension_current = 0;
			ret = cpm_brightness_helper_set_value ("set-brightness", brightness->priv->extension_current);
		}
		if (hw_changed != NULL)
			*hw_changed = ret;
		brightness->priv->cache_trusted = FALSE;
		goto out;
	}
out:
	return ret;
}


/**
 * cpm_brightness_may_have_changed:
 **/
static void
cpm_brightness_may_have_changed (CpmBrightness *brightness)
{
	gboolean ret;
	guint percentage;
	ret = cpm_brightness_get (brightness, &percentage);
	if (!ret) {
		egg_warning ("failed to get output");
		return;
	}
	egg_debug ("emitting brightness-changed (%i)", percentage);
	g_signal_emit (brightness, signals [BRIGHTNESS_CHANGED], 0, percentage);
}

/**
 * cpm_brightness_filter_xevents:
 **/
static CdkFilterReturn
cpm_brightness_filter_xevents (CdkXEvent *xevent G_GNUC_UNUSED,
			       CdkEvent  *event,
			       gpointer   data)
{
	CpmBrightness *brightness = CPM_BRIGHTNESS (data);
	if (event->type == CDK_NOTHING)
		return CDK_FILTER_CONTINUE;
	cpm_brightness_may_have_changed (brightness);
	return CDK_FILTER_CONTINUE;
}


static void cpm_brightness_update_cache (CpmBrightness *brightness);

/**
 * cpm_brightness_monitors_changed:
 **/
static void
cpm_brightness_monitors_changed (CdkScreen     *screen G_GNUC_UNUSED,
				 CpmBrightness *brightness)
{
	g_return_if_fail (CPM_IS_BRIGHTNESS (brightness));
	cpm_brightness_update_cache (brightness);
}

/**
 * cpm_brightness_update_cache:
 **/
static void
cpm_brightness_update_cache (CpmBrightness *brightness)
{
	guint length;
	Window root;
	CdkScreen *gscreen;
	CdkDisplay *display;
	XRRScreenResources *resource;

	g_return_if_fail (CPM_IS_BRIGHTNESS (brightness));

	/* invalidate and remove all the previous entries */
	length = brightness->priv->resources->len;
	if (length > 0)
		g_ptr_array_set_size (brightness->priv->resources, 0);

	display = cdk_display_get_default ();
	gscreen = cdk_display_get_default_screen (display);

	/* if we have not setup the changed on the monitor, set it here */
	if (g_object_get_data (G_OBJECT (gscreen), "gpk-set-monitors-changed") == NULL) {
		egg_debug ("watching ::monitors_changed on %p", gscreen);
		g_object_set_data (G_OBJECT (gscreen), "gpk-set-monitors-changed", (gpointer) "true");
		g_signal_connect (G_OBJECT (gscreen), "monitors_changed",
				  G_CALLBACK (cpm_brightness_monitors_changed), brightness);
	}

	root = RootWindow (brightness->priv->dpy, 0);

	cdk_x11_display_error_trap_push (display);
	resource = XRRGetScreenResourcesCurrent (brightness->priv->dpy, root);
	if (cdk_x11_display_error_trap_pop (display) || resource == NULL) {
		egg_warning ("failed to XRRGetScreenResourcesCurrent");
	}

	if (resource != NULL) {
		egg_debug ("adding resource %p", resource);
		g_ptr_array_add (brightness->priv->resources, resource);
	}
}

/**
 * cpm_brightness_has_hw:
 **/
gboolean
cpm_brightness_has_hw (CpmBrightness *brightness)
{
	g_return_val_if_fail (CPM_IS_BRIGHTNESS (brightness), FALSE);

	/* use XRandR first */
	if (brightness->priv->has_extension)
		return TRUE;

	/* fallback to legacy access */
	if (brightness->priv->extension_levels < 0)
		brightness->priv->extension_levels = cpm_brightness_helper_get_value ("get-max-brightness");
	if (brightness->priv->extension_levels > 0)
		return TRUE;
	return FALSE;
}

/**
 * cpm_brightness_finalize:
 **/
static void
cpm_brightness_finalize (GObject *object)
{
	CpmBrightness *brightness;
	g_return_if_fail (object != NULL);
	g_return_if_fail (CPM_IS_BRIGHTNESS (object));
	brightness = CPM_BRIGHTNESS (object);
	g_ptr_array_unref (brightness->priv->resources);
	cdk_window_remove_filter (brightness->priv->root_window,
				  cpm_brightness_filter_xevents, brightness);
	G_OBJECT_CLASS (cpm_brightness_parent_class)->finalize (object);
}

/**
 * cpm_brightness_class_init:
 **/
static void
cpm_brightness_class_init (CpmBrightnessClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = cpm_brightness_finalize;

	signals [BRIGHTNESS_CHANGED] =
		g_signal_new ("brightness-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmBrightnessClass, brightness_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
}

/**
 * cpm_brightness_init:
 * @brightness: This brightness class instance
 **/
static void
cpm_brightness_init (CpmBrightness *brightness)
{
	CdkScreen *screen;
	CdkDisplay *display;
	int event_base;
	int ignore;

	brightness->priv = cpm_brightness_get_instance_private (brightness);

	brightness->priv->cache_trusted = FALSE;
	brightness->priv->has_changed_events = FALSE;
	brightness->priv->cache_percentage = 0;
	brightness->priv->hw_changed = FALSE;
	brightness->priv->extension_levels = -1;
	brightness->priv->resources = g_ptr_array_new_with_free_func ((GDestroyNotify) XRRFreeScreenResources);

	/* can we do this */
	brightness->priv->has_extension = cpm_brightness_setup_display (brightness);
	if (brightness->priv->has_extension == FALSE)
		egg_debug ("no XRANDR extension");

	screen = cdk_screen_get_default ();
	brightness->priv->root_window = cdk_screen_get_root_window (screen);
	display = cdk_display_get_default ();

	/* as we a filtering by a window, we have to add an event type */
	if (!XRRQueryExtension (CDK_DISPLAY_XDISPLAY (cdk_display_get_default()), &event_base, &ignore)) {
		egg_warning ("can't get event_base for XRR");
	}
	cdk_x11_register_standard_event_type (display, event_base, RRNotify + 1);
	cdk_window_add_filter (brightness->priv->root_window,
			       cpm_brightness_filter_xevents, brightness);

	/* don't abort on error */
	cdk_x11_display_error_trap_push (display);
	XRRSelectInput (CDK_DISPLAY_XDISPLAY (cdk_display_get_default()),
			CDK_WINDOW_XID (brightness->priv->root_window),
			RRScreenChangeNotifyMask |
			RROutputPropertyNotifyMask); /* <--- the only one we need, but see rh:345551 */
	cdk_display_flush (display);
	if (cdk_x11_display_error_trap_pop (display))
		egg_warning ("failed to select XRRSelectInput");

	/* create cache of XRRScreenResources as XRRGetScreenResources() is slow */
	cpm_brightness_update_cache (brightness);
}

/**
 * cpm_brightness_new:
 * Return value: A new brightness class instance.
 * Can return NULL if no suitable hardware is found.
 **/
CpmBrightness *
cpm_brightness_new (void)
{
	if (cpm_brightness_object != NULL) {
		g_object_ref (cpm_brightness_object);
	} else {
		cpm_brightness_object = g_object_new (CPM_TYPE_BRIGHTNESS, NULL);
		g_object_add_weak_pointer (cpm_brightness_object, &cpm_brightness_object);
	}
	return CPM_BRIGHTNESS (cpm_brightness_object);
}

