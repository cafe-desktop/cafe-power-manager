/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2005-2009 Richard Hughes <richard@hughsie.com>
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

#include <glib.h>
#include <glib/gi18n.h>

#include "egg-debug.h"
#include "egg-idletime.h"

#include "cpm-idle.h"
#include "cpm-load.h"
#include "cpm-session.h"

/* Sets the idle percent limit, i.e. how hard the computer can work
   while considered "at idle" */
#define CPM_IDLE_CPU_LIMIT			5
#define	CPM_IDLE_IDLETIME_ID			1

struct CpmIdlePrivate
{
	EggIdletime	*idletime;
	CpmLoad		*load;
	CpmSession	*session;
	CpmIdleMode	 mode;
	guint		 timeout_dim;		/* in seconds */
	guint		 timeout_blank;		/* in seconds */
	guint		 timeout_sleep;		/* in seconds */
	guint		 timeout_blank_id;
	guint		 timeout_sleep_id;
	gboolean	 x_idle;
	gboolean	 check_type_cpu;
};

enum {
	IDLE_CHANGED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };
static gpointer cpm_idle_object = NULL;

G_DEFINE_TYPE_WITH_PRIVATE (CpmIdle, cpm_idle, G_TYPE_OBJECT)

/**
 * cpm_idle_mode_to_string:
 **/
static const gchar *
cpm_idle_mode_to_string (CpmIdleMode mode)
{
	if (mode == CPM_IDLE_MODE_NORMAL)
		return "normal";
	if (mode == CPM_IDLE_MODE_DIM)
		return "dim";
	if (mode == CPM_IDLE_MODE_BLANK)
		return "blank";
	if (mode == CPM_IDLE_MODE_SLEEP)
		return "sleep";
	return "unknown";
}

/**
 * cpm_idle_set_mode:
 * @mode: The new mode, e.g. CPM_IDLE_MODE_SLEEP
 **/
static void
cpm_idle_set_mode (CpmIdle *idle, CpmIdleMode mode)
{
	g_return_if_fail (CPM_IS_IDLE (idle));

	if (mode != idle->priv->mode) {
		idle->priv->mode = mode;
		egg_debug ("Doing a state transition: %s", cpm_idle_mode_to_string (mode));
		g_signal_emit (idle, signals [IDLE_CHANGED], 0, mode);
	}
}

/**
 * cpm_idle_set_check_cpu:
 * @check_type_cpu: If we should check the CPU before mode becomes
 *		    CPM_IDLE_MODE_SLEEP and the event is done.
 **/
void
cpm_idle_set_check_cpu (CpmIdle *idle, gboolean check_type_cpu)
{
	g_return_if_fail (CPM_IS_IDLE (idle));
	egg_debug ("Setting the CPU load check to %i", check_type_cpu);
	idle->priv->check_type_cpu = check_type_cpu;
}

/**
 * cpm_idle_get_mode:
 * Return value: The current mode, e.g. CPM_IDLE_MODE_SLEEP
 **/
CpmIdleMode
cpm_idle_get_mode (CpmIdle *idle)
{
	return idle->priv->mode;
}

/**
 * cpm_idle_blank_cb:
 **/
static gboolean
cpm_idle_blank_cb (CpmIdle *idle)
{
	if (idle->priv->mode > CPM_IDLE_MODE_BLANK) {
		egg_debug ("ignoring current mode %s", cpm_idle_mode_to_string (idle->priv->mode));
		return FALSE;
	}
	cpm_idle_set_mode (idle, CPM_IDLE_MODE_BLANK);
	return FALSE;
}

/**
 * cpm_idle_sleep_cb:
 **/
static gboolean
cpm_idle_sleep_cb (CpmIdle *idle)
{
	gdouble load;
	gboolean ret = FALSE;

	/* get our computed load value */
	if (idle->priv->check_type_cpu) {
		load = cpm_load_get_current (idle->priv->load);
		if (load > CPM_IDLE_CPU_LIMIT) {
			/* check if system is "idle" enough */
			egg_debug ("Detected that the CPU is busy");
			ret = TRUE;
			goto out;
		}
	}
	cpm_idle_set_mode (idle, CPM_IDLE_MODE_SLEEP);
out:
	return ret;
}

/**
 * cpm_idle_evaluate:
 **/
static void
cpm_idle_evaluate (CpmIdle *idle)
{
	gboolean is_idle;
	gboolean is_idle_inhibited;
	gboolean is_suspend_inhibited;

	is_idle = cpm_session_get_idle (idle->priv->session);
	is_idle_inhibited = cpm_session_get_idle_inhibited (idle->priv->session);
	is_suspend_inhibited = cpm_session_get_suspend_inhibited (idle->priv->session);
	egg_debug ("session_idle=%i, idle_inhibited=%i, suspend_inhibited=%i, x_idle=%i", is_idle, is_idle_inhibited, is_suspend_inhibited, idle->priv->x_idle);

	/* check we are really idle */
	if (!idle->priv->x_idle) {
		cpm_idle_set_mode (idle, CPM_IDLE_MODE_NORMAL);
		egg_debug ("X not idle");
		if (idle->priv->timeout_blank_id != 0) {
			g_source_remove (idle->priv->timeout_blank_id);
			idle->priv->timeout_blank_id = 0;
		}
		if (idle->priv->timeout_sleep_id != 0) {
			g_source_remove (idle->priv->timeout_sleep_id);
			idle->priv->timeout_sleep_id = 0;
		}
		goto out;
	}

	/* are we inhibited from going idle */
	if (is_idle_inhibited) {
		egg_debug ("inhibited, so using normal state");
		cpm_idle_set_mode (idle, CPM_IDLE_MODE_NORMAL);
		if (idle->priv->timeout_blank_id != 0) {
			g_source_remove (idle->priv->timeout_blank_id);
			idle->priv->timeout_blank_id = 0;
		}
		if (idle->priv->timeout_sleep_id != 0) {
			g_source_remove (idle->priv->timeout_sleep_id);
			idle->priv->timeout_sleep_id = 0;
		}
		goto out;
	}

	/* normal to dim */
	if (idle->priv->mode == CPM_IDLE_MODE_NORMAL) {
		egg_debug ("normal to dim");
		cpm_idle_set_mode (idle, CPM_IDLE_MODE_DIM);
	}

	/* set up blank callback even when session is not idle,
	 * but only if we actually want to blank. */
	if (idle->priv->timeout_blank_id == 0 &&
	    idle->priv->timeout_blank != 0) {
		egg_debug ("setting up blank callback for %is", idle->priv->timeout_blank);
		idle->priv->timeout_blank_id = g_timeout_add_seconds (idle->priv->timeout_blank,
								      (GSourceFunc) cpm_idle_blank_cb, idle);
		g_source_set_name_by_id (idle->priv->timeout_blank_id, "[CpmIdle] blank");
	}

	/* are we inhibited from sleeping */
	if (is_suspend_inhibited) {
		egg_debug ("suspend inhibited");
		if (idle->priv->timeout_sleep_id != 0) {
			g_source_remove (idle->priv->timeout_sleep_id);
			idle->priv->timeout_sleep_id = 0;
		}
	} else if (is_idle) {
	/* only do the sleep timeout when the session is idle and we aren't inhibited from sleeping */
		if (idle->priv->timeout_sleep_id == 0 &&
		    idle->priv->timeout_sleep != 0) {
			egg_debug ("setting up sleep callback %is", idle->priv->timeout_sleep);
			idle->priv->timeout_sleep_id = g_timeout_add_seconds (idle->priv->timeout_sleep,
									      (GSourceFunc) cpm_idle_sleep_cb, idle);
			g_source_set_name_by_id (idle->priv->timeout_sleep_id, "[CpmIdle] sleep");
		}
	}
out:
	return;
}

/**
 *  cpm_idle_adjust_timeout_dim:
 *  @idle_time: The new timeout we want to set, in seconds.
 *  @timeout: Current idle time, in seconds.
 *
 *  On slow machines, or machines that have lots to load duing login,
 *  the current idle time could be bigger than the requested timeout.
 *  In this case the scheduled idle timeout will never fire, unless
 *  some user activity (keyboard, mouse) resets the current idle time.
 *  Instead of relying on user activity to correct this issue, we need
 *  to adjust timeout, as related to current idle time, so the idle
 *  timeout will fire as designed.
 *
 *  Return value: timeout to set, adjusted acccording to current idle time.
 **/
static guint
cpm_idle_adjust_timeout_dim (guint idle_time, guint timeout)
{
	/* allow 2 sec margin for messaging delay. */
	idle_time += 2;

	/* Double timeout until it's larger than current idle time.
	 * Give up for ultra slow machines. (86400 sec = 24 hours) */
	while (timeout < idle_time && timeout < 86400 && timeout > 0) {
		timeout *= 2;
	}
	return timeout;
}

/**
 * cpm_idle_set_timeout_dim:
 * @timeout: The new timeout we want to set, in seconds
 **/
gboolean
cpm_idle_set_timeout_dim (CpmIdle *idle, guint timeout)
{
	gint64 idle_time_in_msec;
	guint timeout_adjusted;

	g_return_val_if_fail (CPM_IS_IDLE (idle), FALSE);

	idle_time_in_msec = egg_idletime_get_time (idle->priv->idletime);
	timeout_adjusted  = cpm_idle_adjust_timeout_dim (idle_time_in_msec / 1000, timeout);
	egg_debug ("Current idle time=%lldms, timeout was %us, becomes %us after adjustment",
		   (long long int)idle_time_in_msec, timeout, timeout_adjusted);
	timeout = timeout_adjusted;

	egg_debug ("Setting dim idle timeout: %ds", timeout);
	if (idle->priv->timeout_dim != timeout) {
		idle->priv->timeout_dim = timeout;

		if (timeout > 0)
			egg_idletime_alarm_set (idle->priv->idletime, CPM_IDLE_IDLETIME_ID, timeout * 1000);
		else
			egg_idletime_alarm_remove (idle->priv->idletime, CPM_IDLE_IDLETIME_ID);
	}
	return TRUE;
}

/**
 * cpm_idle_set_timeout_blank:
 * @timeout: The new timeout we want to set, in seconds
 **/
gboolean
cpm_idle_set_timeout_blank (CpmIdle *idle, guint timeout)
{
	g_return_val_if_fail (CPM_IS_IDLE (idle), FALSE);

	egg_debug ("Setting blank idle timeout: %ds", timeout);
	if (idle->priv->timeout_blank != timeout) {
		idle->priv->timeout_blank = timeout;
		cpm_idle_evaluate (idle);
	}
	return TRUE;
}

/**
 * cpm_idle_set_timeout_sleep:
 * @timeout: The new timeout we want to set, in seconds
 **/
gboolean
cpm_idle_set_timeout_sleep (CpmIdle *idle, guint timeout)
{
	g_return_val_if_fail (CPM_IS_IDLE (idle), FALSE);

	egg_debug ("Setting sleep idle timeout: %ds", timeout);
	if (idle->priv->timeout_sleep != timeout) {
		idle->priv->timeout_sleep = timeout;
		cpm_idle_evaluate (idle);
	}
	return TRUE;
}

/**
 * cpm_idle_session_idle_changed_cb:
 * @is_idle: If the session is idle
 *
 * The SessionIdleChanged callback from cafe-session.
 **/
static void
cpm_idle_session_idle_changed_cb (CpmSession *session G_GNUC_UNUSED,
				  gboolean    is_idle,
				  CpmIdle    *idle)
{
	egg_debug ("Received cafe session idle changed: %i", is_idle);
	idle->priv->x_idle = is_idle;
	cpm_idle_evaluate (idle);
}

/**
 * cpm_idle_session_inhibited_changed_cb:
 **/
static void
cpm_idle_session_inhibited_changed_cb (CpmSession *session G_GNUC_UNUSED,
				       gboolean    is_idle_inhibited,
				       gboolean    is_suspend_inhibited,
				       CpmIdle    *idle)
{
	egg_debug ("Received cafe session inhibited changed: idle=(%i), suspend=(%i)", is_idle_inhibited, is_suspend_inhibited);
	cpm_idle_evaluate (idle);
}

/**
 * cpm_idle_idletime_alarm_expired_cb:
 *
 * We're idle, something timed out
 **/
static void
cpm_idle_idletime_alarm_expired_cb (EggIdletime *idletime G_GNUC_UNUSED,
				    guint        alarm_id,
				    CpmIdle     *idle)
{
	egg_debug ("idletime alarm: %i", alarm_id);

	/* set again */
	idle->priv->x_idle = TRUE;
	cpm_idle_evaluate (idle);
}

/**
 * cpm_idle_idletime_reset_cb:
 *
 * We're no longer idle, the user moved
 **/
static void
cpm_idle_idletime_reset_cb (EggIdletime *idletime G_GNUC_UNUSED,
			    CpmIdle     *idle)
{
	egg_debug ("idletime reset");

	idle->priv->x_idle = FALSE;
	cpm_idle_evaluate (idle);
}

/**
 * cpm_idle_finalize:
 * @object: This class instance
 **/
static void
cpm_idle_finalize (GObject *object)
{
	CpmIdle *idle;

	g_return_if_fail (object != NULL);
	g_return_if_fail (CPM_IS_IDLE (object));

	idle = CPM_IDLE (object);

	g_return_if_fail (idle->priv != NULL);

	if (idle->priv->timeout_blank_id != 0) {
		g_source_remove (idle->priv->timeout_blank_id);
		idle->priv->timeout_blank_id = 0;
	}

	if (idle->priv->timeout_sleep_id != 0) {
		g_source_remove (idle->priv->timeout_sleep_id);
		idle->priv->timeout_sleep_id = 0;
	}

	g_object_unref (idle->priv->load);
	g_object_unref (idle->priv->session);

	egg_idletime_alarm_remove (idle->priv->idletime, CPM_IDLE_IDLETIME_ID);
	g_object_unref (idle->priv->idletime);

	G_OBJECT_CLASS (cpm_idle_parent_class)->finalize (object);
}

/**
 * cpm_idle_class_init:
 * @klass: This class instance
 **/
static void
cpm_idle_class_init (CpmIdleClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = cpm_idle_finalize;

	signals [IDLE_CHANGED] =
		g_signal_new ("idle-changed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (CpmIdleClass, idle_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);
}

/**
 * cpm_idle_init:
 *
 * Gets a DBUS connection, and aquires the session connection so we can
 * get session changed events.
 *
 **/
static void
cpm_idle_init (CpmIdle *idle)
{
	idle->priv = cpm_idle_get_instance_private (idle);

	idle->priv->timeout_dim = G_MAXUINT;
	idle->priv->timeout_blank = G_MAXUINT;
	idle->priv->timeout_sleep = G_MAXUINT;
	idle->priv->timeout_blank_id = 0;
	idle->priv->timeout_sleep_id = 0;
	idle->priv->x_idle = FALSE;
	idle->priv->load = cpm_load_new ();
	idle->priv->session = cpm_session_new ();
	g_signal_connect (idle->priv->session, "idle-changed", G_CALLBACK (cpm_idle_session_idle_changed_cb), idle);
	g_signal_connect (idle->priv->session, "inhibited-changed", G_CALLBACK (cpm_idle_session_inhibited_changed_cb), idle);

	idle->priv->idletime = egg_idletime_new ();
	g_signal_connect (idle->priv->idletime, "reset", G_CALLBACK (cpm_idle_idletime_reset_cb), idle);
	g_signal_connect (idle->priv->idletime, "alarm-expired", G_CALLBACK (cpm_idle_idletime_alarm_expired_cb), idle);

	cpm_idle_evaluate (idle);
}

/**
 * cpm_idle_new:
 * Return value: A new CpmIdle instance.
 **/
CpmIdle *
cpm_idle_new (void)
{
	if (cpm_idle_object != NULL) {
		g_object_ref (cpm_idle_object);
	} else {
		cpm_idle_object = g_object_new (CPM_TYPE_IDLE, NULL);
		g_object_add_weak_pointer (cpm_idle_object, &cpm_idle_object);
	}
	return CPM_IDLE (cpm_idle_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"
#include "cpm-dpms.h"

static CpmIdleMode _mode = 0;

static void
cpm_idle_test_idle_changed_cb (CpmIdle *idle, CpmIdleMode mode, EggTest *test)
{
	_mode = mode;
	egg_debug ("idle-changed %s", cpm_idle_mode_to_string (mode));
	egg_test_loop_quit (test);
}

static gboolean
cpm_idle_test_delay_cb (EggTest *test)
{
	egg_warning ("timing out");
	egg_test_loop_quit (test);
	return FALSE;
}

void
cpm_idle_test (gpointer data)
{
	CpmIdle *idle;
	gboolean ret;
	EggTest *test = (EggTest *) data;
	CpmIdleMode mode;
	CpmDpms *dpms;

	if (!egg_test_start (test, "CpmIdle"))
		return;

	/************************************************************/
	egg_test_title (test, "get object");
	idle = cpm_idle_new ();
	if (idle != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got no object");

	/* set up defaults */
	cpm_idle_set_check_cpu (idle, FALSE);
	cpm_idle_set_timeout_dim (idle, 4);
	cpm_idle_set_timeout_blank (idle, 5);
	cpm_idle_set_timeout_sleep (idle, 15);
	g_signal_connect (idle, "idle-changed",
			  G_CALLBACK (cpm_idle_test_idle_changed_cb), test);

	/************************************************************/
	egg_test_title (test, "check cpu type");
	egg_test_assert (test, (idle->priv->check_type_cpu == FALSE));

	/************************************************************/
	egg_test_title (test, "check timeout dim");
	egg_test_assert (test, (idle->priv->timeout_dim == 4));

	/************************************************************/
	egg_test_title (test, "check timeout blank");
	egg_test_assert (test, (idle->priv->timeout_blank == 5));

	/************************************************************/
	egg_test_title (test, "check timeout sleep");
	egg_test_assert (test, (idle->priv->timeout_sleep == 15));

	/************************************************************/
	egg_test_title (test, "check x_idle");
	egg_test_assert (test, (idle->priv->x_idle == FALSE));

	/************************************************************/
	egg_test_title (test, "check blank id");
	egg_test_assert (test, (idle->priv->timeout_blank_id == 0));

	/************************************************************/
	egg_test_title (test, "check sleep id");
	egg_test_assert (test, (idle->priv->timeout_sleep_id == 0));

	/************************************************************/
	egg_test_title (test, "check normal at startup");
	mode = cpm_idle_get_mode (idle);
	if (mode == CPM_IDLE_MODE_NORMAL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "mode: %s", cpm_idle_mode_to_string (mode));

	/************************************************************/
	g_print ("*****************************\n");
	g_print ("*** DO NOT MOVE THE MOUSE ***\n");
	g_print ("*****************************\n");
	egg_test_loop_wait (test, 2000 + 10000);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "check callback mode");
	if (_mode == CPM_IDLE_MODE_DIM)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "mode: %s", cpm_idle_mode_to_string (mode));

	/************************************************************/
	egg_test_title (test, "check current mode");
	mode = cpm_idle_get_mode (idle);
	if (mode == CPM_IDLE_MODE_DIM)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "mode: %s", cpm_idle_mode_to_string (mode));

	/************************************************************/
	egg_test_title (test, "check x_idle");
	egg_test_assert (test, (idle->priv->x_idle == TRUE));

	/************************************************************/
	egg_test_title (test, "check blank id");
	egg_test_assert (test, (idle->priv->timeout_blank_id != 0));

	/************************************************************/
	egg_test_title (test, "check sleep id");
	egg_test_assert (test, (idle->priv->timeout_sleep_id == 0));

	/************************************************************/
	egg_test_loop_wait (test, 5000 + 1000);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "check callback mode");
	if (_mode == CPM_IDLE_MODE_BLANK)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "mode: %s", cpm_idle_mode_to_string (mode));

	/************************************************************/
	egg_test_title (test, "check current mode");
	mode = cpm_idle_get_mode (idle);
	if (mode == CPM_IDLE_MODE_BLANK)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "mode: %s", cpm_idle_mode_to_string (mode));

	/************************************************************/
	g_print ("**********************\n");
	g_print ("*** MOVE THE MOUSE ***\n");
	g_print ("**********************\n");
	egg_test_loop_wait (test, G_MAXUINT);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "check callback mode");
	if (_mode == CPM_IDLE_MODE_NORMAL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "mode: %s", cpm_idle_mode_to_string (mode));

	/************************************************************/
	egg_test_title (test, "check current mode");
	mode = cpm_idle_get_mode (idle);
	if (mode == CPM_IDLE_MODE_NORMAL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "mode: %s", cpm_idle_mode_to_string (mode));

	/************************************************************/
	egg_test_title (test, "check x_idle");
	egg_test_assert (test, (idle->priv->x_idle == FALSE));

	/************************************************************/
	egg_test_title (test, "check blank id");
	egg_test_assert (test, (idle->priv->timeout_blank_id == 0));

	/************************************************************/
	g_print ("*****************************\n");
	g_print ("*** DO NOT MOVE THE MOUSE ***\n");
	g_print ("*****************************\n");
	egg_test_loop_wait (test, 4000 + 1500);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "check current mode");
	mode = cpm_idle_get_mode (idle);
	if (mode == CPM_IDLE_MODE_DIM)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "mode: %s", cpm_idle_mode_to_string (mode));

	/************************************************************/
	egg_test_title (test, "check x_idle");
	egg_test_assert (test, (idle->priv->x_idle == TRUE));

	egg_test_loop_wait (test, 15000);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "check current mode");
	mode = cpm_idle_get_mode (idle);
	if (mode == CPM_IDLE_MODE_BLANK)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "mode: %s", cpm_idle_mode_to_string (mode));

	/************************************************************/
	egg_test_title (test, "set dpms off");
	dpms = cpm_dpms_new ();
	ret = cpm_dpms_set_mode (dpms, CPM_DPMS_MODE_OFF, NULL);
	egg_test_assert (test, ret);

	/* wait for normal event to be suppressed */
	g_timeout_add (2000, (GSourceFunc) cpm_idle_test_delay_cb, test);
	egg_test_loop_wait (test, G_MAXUINT);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "check current mode");
	mode = cpm_idle_get_mode (idle);
	if (mode == CPM_IDLE_MODE_BLANK)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "mode: %s", cpm_idle_mode_to_string (mode));

	/************************************************************/
	egg_test_title (test, "check x_idle");
	egg_test_assert (test, (idle->priv->x_idle == TRUE));

	cpm_dpms_set_mode (dpms, CPM_DPMS_MODE_ON, NULL);

	g_object_unref (idle);
	g_object_unref (dpms);

	egg_test_end (test);
}

#endif

