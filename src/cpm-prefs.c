/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005 Jaap Haitsma <jaap@haitsma.org>
 * Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2005-2008 Richard Hughes <richard@hughsie.com>
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
#  include <config.h>
#endif

#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <ctk/ctk.h>

#include "cpm-common.h"
#include "egg-debug.h"
#include "cpm-prefs-core.h"

/**
 * cpm_prefs_help_cb
 * @prefs: This prefs class instance
 *
 * What to do when help is requested
 **/
static void
cpm_prefs_help_cb (CpmPrefs *prefs G_GNUC_UNUSED)
{
	cpm_help_display ("preferences");
}

/**
 * cpm_prefs_activated_cb
 * @prefs: This prefs class instance
 *
 * We have been asked to show the window
 **/
static void
cpm_prefs_activated_cb (CtkApplication *app, CpmPrefs *prefs)
{
	cpm_prefs_activate_window (app, prefs);
}

/**
 * main:
 **/
int
main (int argc, char **argv)
{
	gboolean verbose = FALSE;
	GOptionContext *context;
	CpmPrefs *prefs = NULL;
	gboolean ret;
	CtkApplication *app;
	CtkWidget *window;
	gint status;

	const GOptionEntry options[] = {
		{ "verbose", '\0', 0, G_OPTION_ARG_NONE, &verbose,
		  N_("Show extra debugging information"), NULL },
		{ NULL}
	};

	context = g_option_context_new (N_("CAFE Power Preferences"));

	bindtextdomain (GETTEXT_PACKAGE, CAFELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	g_option_context_set_translation_domain(context, GETTEXT_PACKAGE);
	g_option_context_add_main_entries (context, options, GETTEXT_PACKAGE);
	g_option_context_add_group (context, ctk_get_option_group (FALSE));
	g_option_context_parse (context, &argc, &argv, NULL);

	egg_debug_init (verbose);

	cdk_init (&argc, &argv);
	app = ctk_application_new("org.cafe.PowerManager.Preferences", 0);

	prefs = cpm_prefs_new ();

	window = cpm_window (prefs);
	g_signal_connect (app, "activate",
			  G_CALLBACK (cpm_prefs_activated_cb), prefs);
	g_signal_connect (prefs, "action-help",
			  G_CALLBACK (cpm_prefs_help_cb), prefs);
	g_signal_connect_swapped (prefs, "action-close",
			  G_CALLBACK (ctk_widget_destroy), window);

	g_object_set (ctk_settings_get_default (), "ctk-button-images", TRUE, NULL);

	status = g_application_run (G_APPLICATION (app), argc, argv);
	g_object_unref (prefs);

	g_object_unref (app);

/* seems to not work...
	g_option_context_free (context); */

	return status;
}
