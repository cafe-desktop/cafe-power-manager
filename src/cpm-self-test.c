/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

#include <glib.h>
#include <glib-object.h>
#include <ctk/ctk.h>
#include "egg-test.h"
#include "egg-debug.h"

#include "cpm-screensaver.h"

/* prototypes */
void egg_precision_test (EggTest *test);
void egg_discrete_test (EggTest *test);
void egg_color_test (EggTest *test);
void egg_array_float_test (EggTest *test);
void egg_idletime_test (EggTest *test);

void cpm_common_test (EggTest *test);
void cpm_idle_test (EggTest *test);
void cpm_phone_test (EggTest *test);
void cpm_dpms_test (EggTest *test);
void cpm_graph_widget_test (EggTest *test);
void cpm_proxy_test (EggTest *test);
void cpm_hal_manager_test (EggTest *test);
void cpm_device_test (EggTest *test);
void cpm_device_teststore (EggTest *test);

int
main (int argc, char **argv)
{
	EggTest *test;

	test = egg_test_init ();
	egg_debug_init (TRUE);

	/* needed for DPMS checks */
	ctk_init (&argc, &argv);

	/* tests go here */
	egg_precision_test (test);
	egg_discrete_test (test);
	egg_color_test (test);
	egg_array_float_test (test);
//	egg_idletime_test (test);

	cpm_common_test (test);
//	cpm_idle_test (test);
	cpm_phone_test (test);
//	cpm_dpms_test (test);
//	cpm_graph_widget_test (test);
//	cpm_screensaver_test (test);

#if 0
	cpm_proxy_test (test);
	cpm_hal_manager_test (test);
	cpm_device_test (test);
	cpm_device_teststore (test);
#endif

	return (egg_test_finish (test));
}

