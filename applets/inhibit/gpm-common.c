/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005-2007 Richard Hughes <richard@hughsie.com>
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
#include <string.h>
#include <glib/gi18n.h>
#include <cdk/cdk.h>
#include <ctk/ctk.h>

#include "egg-debug.h"
#include "gpm-common.h"

/**
 * gpm_help_display:
 * @link_id: Subsection of cafe-power-manager help section
 **/
void
gpm_help_display (const gchar *link_id)
{
	GError *error = NULL;
	gchar *uri;

	if (link_id != NULL)
		uri = g_strconcat ("help:cafe-power-manager/", link_id, NULL);
	else
		uri = g_strdup ("help:cafe-power-manager");

	ctk_show_uri_on_window (NULL, uri, CDK_CURRENT_TIME, &error);

	if (error != NULL) {
		CtkWidget *d;
		d = ctk_message_dialog_new (NULL, CTK_DIALOG_MODAL | CTK_DIALOG_DESTROY_WITH_PARENT,
					    CTK_MESSAGE_ERROR, CTK_BUTTONS_OK, "%s", error->message);
		ctk_dialog_run (CTK_DIALOG(d));
		ctk_widget_destroy (d);
		g_error_free (error);
	}
	g_free (uri);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
gpm_common_test (gpointer data)
{
	EggTest *test = (EggTest *) data;
	if (egg_test_start (test, "GpmCommon") == FALSE)
		return;

	egg_test_end (test);
}

#endif

