/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2006-2009 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <cdk/cdk.h>
#include <cdk/cdkx.h>

#include <X11/Xproto.h>
#include <X11/extensions/dpms.h>

#include "egg-debug.h"
#include "cpm-dpms.h"

static void   cpm_dpms_finalize  (GObject   *object);

/* until we get a nice event-emitting DPMS extension, we have to poll... */
#define CPM_DPMS_POLL_TIME	10

struct CpmDpmsPrivate
{
	gboolean		 dpms_capable;
	CpmDpmsMode		 mode;
	guint			 timer_id;
	Display			*display;
};

enum {
	MODE_CHANGED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };
static gpointer cpm_dpms_object = NULL;

G_DEFINE_TYPE_WITH_PRIVATE (CpmDpms, cpm_dpms, G_TYPE_OBJECT)

/**
 * cpm_dpms_error_quark:
 **/
GQuark
cpm_dpms_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("cpm_dpms_error");
	return quark;
}

/**
 * cpm_dpms_x11_get_mode:
 **/
static gboolean
cpm_dpms_x11_get_mode (CpmDpms     *dpms,
		       CpmDpmsMode *mode,
		       GError     **error G_GNUC_UNUSED)
{
	CpmDpmsMode result;
	BOOL enabled = FALSE;
	CARD16 state;

	if (dpms->priv->dpms_capable == FALSE) {
		/* Server or monitor can't DPMS -- assume the monitor is on. */
		result = CPM_DPMS_MODE_ON;
		goto out;
	}

	DPMSInfo (dpms->priv->display, &state, &enabled);
	if (!enabled) {
		/* Server says DPMS is disabled -- so the monitor is on. */
		result = CPM_DPMS_MODE_ON;
		goto out;
	}

	switch (state) {
	case DPMSModeOn:
		result = CPM_DPMS_MODE_ON;
		break;
	case DPMSModeStandby:
		result = CPM_DPMS_MODE_STANDBY;
		break;
	case DPMSModeSuspend:
		result = CPM_DPMS_MODE_SUSPEND;
		break;
	case DPMSModeOff:
		result = CPM_DPMS_MODE_OFF;
		break;
	default:
		result = CPM_DPMS_MODE_ON;
		break;
	}
out:
	if (mode)
		*mode = result;
	return TRUE;
}

/**
 * cpm_dpms_x11_set_mode:
 **/
static gboolean
cpm_dpms_x11_set_mode (CpmDpms *dpms, CpmDpmsMode mode, GError **error)
{
	CpmDpmsMode current_mode;
	CARD16 state;
	CARD16 current_state;
	BOOL current_enabled;

	if (!dpms->priv->dpms_capable) {
		egg_debug ("not DPMS capable");
		g_set_error (error, CPM_DPMS_ERROR, CPM_DPMS_ERROR_GENERAL,
			     "Display is not DPMS capable");
		return FALSE;
	}

	if (!DPMSInfo (dpms->priv->display, &current_state, &current_enabled)) {
		egg_debug ("couldn't get DPMS info");
		g_set_error (error, CPM_DPMS_ERROR, CPM_DPMS_ERROR_GENERAL,
			     "Unable to get DPMS state");
		return FALSE;
	}

	if (!current_enabled) {
		egg_debug ("DPMS not enabled");
		g_set_error (error, CPM_DPMS_ERROR, CPM_DPMS_ERROR_GENERAL,
			     "DPMS is not enabled");
		return FALSE;
	}

	switch (mode) {
	case CPM_DPMS_MODE_ON:
		state = DPMSModeOn;
		break;
	case CPM_DPMS_MODE_STANDBY:
		state = DPMSModeStandby;
		break;
	case CPM_DPMS_MODE_SUSPEND:
		state = DPMSModeSuspend;
		break;
	case CPM_DPMS_MODE_OFF:
		state = DPMSModeOff;
		break;
	default:
		state = DPMSModeOn;
		break;
	}

	cpm_dpms_x11_get_mode (dpms, &current_mode, NULL);
	if (current_mode != mode) {
		if (! DPMSForceLevel (dpms->priv->display, state)) {
			g_set_error (error, CPM_DPMS_ERROR, CPM_DPMS_ERROR_GENERAL,
				     "Could not change DPMS mode");
			return FALSE;
		}
		XSync (dpms->priv->display, FALSE);
	}

	return TRUE;
}

/**
 * cpm_dpms_set_mode:
 **/
gboolean
cpm_dpms_set_mode (CpmDpms *dpms, CpmDpmsMode mode, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (CPM_IS_DPMS (dpms), FALSE);

	if (mode == CPM_DPMS_MODE_UNKNOWN) {
		egg_debug ("mode unknown");
		g_set_error (error, CPM_DPMS_ERROR, CPM_DPMS_ERROR_GENERAL,
			     "Unknown DPMS mode");
		return FALSE;
	}

	ret = cpm_dpms_x11_set_mode (dpms, mode, error);
	return ret;
}

/**
 * cpm_dpms_get_mode:
 **/
gboolean
cpm_dpms_get_mode (CpmDpms *dpms, CpmDpmsMode *mode, GError **error)
{
	gboolean ret;
	if (mode)
		*mode = CPM_DPMS_MODE_UNKNOWN;
	ret = cpm_dpms_x11_get_mode (dpms, mode, error);
	return ret;
}

/**
 * cpm_dpms_poll_mode_cb:
 **/
static gboolean
cpm_dpms_poll_mode_cb (CpmDpms *dpms)
{
	gboolean ret;
	CpmDpmsMode mode;
	GError *error = NULL;

	/* Try again */
	ret = cpm_dpms_x11_get_mode (dpms, &mode, &error);
	if (!ret) {
		g_clear_error (&error);
		return TRUE;
	}

	if (mode != dpms->priv->mode) {
		dpms->priv->mode = mode;
		g_signal_emit (dpms, signals [MODE_CHANGED], 0, mode);
	}

	return TRUE;
}

/**
 * cpm_dpms_clear_timeouts:
 **/
static gboolean
cpm_dpms_clear_timeouts (CpmDpms *dpms)
{
	gboolean ret = FALSE;

	/* never going to work */
	if (!dpms->priv->dpms_capable) {
		egg_debug ("not DPMS capable");
		goto out;
	}

	egg_debug ("set timeouts to zero");
	ret = DPMSSetTimeouts (dpms->priv->display, 0, 0, 0);

out:
	return ret;
}

/**
 * cpm_dpms_class_init:
 **/
static void
cpm_dpms_class_init (CpmDpmsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = cpm_dpms_finalize;

	signals [MODE_CHANGED] =
		g_signal_new ("mode-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmDpmsClass, mode_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
}

/**
 * cpm_dpms_init:
 **/
static void
cpm_dpms_init (CpmDpms *dpms)
{
	dpms->priv = cpm_dpms_get_instance_private (dpms);

	/* DPMSCapable() can never change for a given display */
	dpms->priv->display = CDK_DISPLAY_XDISPLAY (cdk_display_get_default());
	dpms->priv->dpms_capable = DPMSCapable (dpms->priv->display);
	dpms->priv->timer_id = g_timeout_add_seconds (CPM_DPMS_POLL_TIME, (GSourceFunc)cpm_dpms_poll_mode_cb, dpms);
	g_source_set_name_by_id (dpms->priv->timer_id, "[CpmDpms] poll");

	/* ensure we clear the default timeouts (Standby: 1200s, Suspend: 1800s, Off: 2400s) */
	cpm_dpms_clear_timeouts (dpms);
}

/**
 * cpm_dpms_finalize:
 **/
static void
cpm_dpms_finalize (GObject *object)
{
	CpmDpms *dpms;

	g_return_if_fail (object != NULL);
	g_return_if_fail (CPM_IS_DPMS (object));

	dpms = CPM_DPMS (object);

	g_return_if_fail (dpms->priv != NULL);

	if (dpms->priv->timer_id != 0) {
		g_source_remove (dpms->priv->timer_id);
		dpms->priv->timer_id = 0;
	}

	G_OBJECT_CLASS (cpm_dpms_parent_class)->finalize (object);
}

/**
 * cpm_dpms_new:
 **/
CpmDpms *
cpm_dpms_new (void)
{
	if (cpm_dpms_object != NULL) {
		g_object_ref (cpm_dpms_object);
	} else {
		cpm_dpms_object = g_object_new (CPM_TYPE_DPMS, NULL);
		g_object_add_weak_pointer (cpm_dpms_object, &cpm_dpms_object);
	}
	return CPM_DPMS (cpm_dpms_object);
}


/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
cpm_dpms_test (gpointer data)
{
	CpmDpms *dpms;
	gboolean ret;
	GError *error = NULL;
	EggTest *test = (EggTest *) data;

	if (!egg_test_start (test, "CpmDpms"))
		return;

	/************************************************************/
	egg_test_title (test, "get object");
	dpms = cpm_dpms_new ();
	if (dpms != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got no object");

	/************************************************************/
	egg_test_title (test, "set on");
	ret = cpm_dpms_set_mode (dpms, CPM_DPMS_MODE_ON, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed: %s", error->message);

	g_usleep (2*1000*1000);

	/************************************************************/
	egg_test_title (test, "set STANDBY");
	ret = cpm_dpms_set_mode (dpms, CPM_DPMS_MODE_STANDBY, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed: %s", error->message);

	g_usleep (2*1000*1000);

	/************************************************************/
	egg_test_title (test, "set SUSPEND");
	ret = cpm_dpms_set_mode (dpms, CPM_DPMS_MODE_SUSPEND, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed: %s", error->message);

	g_usleep (2*1000*1000);

	/************************************************************/
	egg_test_title (test, "set OFF");
	ret = cpm_dpms_set_mode (dpms, CPM_DPMS_MODE_OFF, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed: %s", error->message);

	g_usleep (2*1000*1000);

	/************************************************************/
	egg_test_title (test, "set on");
	ret = cpm_dpms_set_mode (dpms, CPM_DPMS_MODE_ON, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed: %s", error->message);

	g_usleep (2*1000*1000);

	g_object_unref (dpms);

	egg_test_end (test);
}

#endif

