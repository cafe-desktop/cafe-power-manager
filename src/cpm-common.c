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
#include "cpm-common.h"

/**
 * cpm_get_timestring:
 * @time_secs: The time value to convert in seconds
 * @cookie: The cookie we are looking for
 *
 * Returns a localised timestring
 *
 * Return value: The time string, e.g. "2 hours 3 minutes"
 **/
gchar *
cpm_get_timestring (guint time_secs)
{
	char* timestring = NULL;
	gint  hours;
	gint  minutes;

	/* Add 0.5 to do rounding */
	minutes = (int) ( ( time_secs / 60.0 ) + 0.5 );

	if (minutes == 0) {
		timestring = g_strdup (_("Unknown time"));
		return timestring;
	}

	if (minutes < 60) {
		timestring = g_strdup_printf (ngettext ("%i minute",
							"%i minutes",
							minutes), minutes);
		return timestring;
	}

	hours = minutes / 60;
	minutes = minutes % 60;

	if (minutes == 0)
		timestring = g_strdup_printf (ngettext (
				"%i hour",
				"%i hours",
				hours), hours);
	else
		/* TRANSLATOR: "%i %s %i %s" are "%i hours %i minutes"
		 * Swap order with "%2$s %2$i %1$s %1$i if needed */
		timestring = g_strdup_printf (_("%i %s %i %s"),
				hours, ngettext ("hour", "hours", hours),
				minutes, ngettext ("minute", "minutes", minutes));
	return timestring;
}

/**
 * cpm_discrete_from_percent:
 * @percentage: The percentage to convert
 * @levels: The number of discrete levels
 *
 * We have to be carefull when converting from %->discrete as precision is very
 * important if we want the highest value.
 *
 * Return value: The discrete value for this percentage.
 **/
guint
cpm_discrete_from_percent (guint percentage, guint levels)
{
    /* for levels < 10 min value is 0 */
    gint factor;
    factor = levels < 10 ? 0 : 1;
    /* check we are in range */
    if (percentage > 100)
        return levels;
    if (levels == 0) {
        g_warning ("levels is 0!");
        return 0;
    }
    return (guint) ((((gfloat) percentage * (gfloat) (levels - factor)) / 100.0f) + 0.5f);
}

/**
 * cpm_discrete_to_percent:
 * @hw: The discrete level
 * @levels: The number of discrete levels
 *
 * We have to be carefull when converting from discrete->%.
 *
 * Return value: The percentage for this discrete value.
 **/
guint
cpm_discrete_to_percent (guint discrete, guint levels)
{
    /* for levels < 10 min value is 0 */
    gint factor;
    factor = levels < 10 ? 0 : 1;
    /* check we are in range */
    if (discrete > levels)
        return 100;
    if (levels == 0) {
        g_warning ("levels is 0!");
        return 0;
    }
    return (guint) (((gfloat) discrete * (100.0f / (gfloat) (levels - factor))) + 0.5f);
}


/**
 * cpm_help_display:
 * @link_id: Subsection of cafe-power-manager help section
 **/
void
cpm_help_display (const gchar *link_id)
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

/**
 * cpm_dialog_page_scroll_event_cb:
 **/
gboolean
cpm_dialog_page_scroll_event_cb (CtkWidget      *widget,
				 CdkEventScroll *event,
				 CtkWindow      *window G_GNUC_UNUSED)
{
        CtkNotebook *notebook = CTK_NOTEBOOK (widget);
        CtkWidget *child, *event_widget, *action_widget;

        child = ctk_notebook_get_nth_page (notebook, ctk_notebook_get_current_page (notebook));
        if (child == NULL)
                return FALSE;

        event_widget = ctk_get_event_widget ((CdkEvent *) event);

        /* Ignore scroll events from the content of the page */
        if (event_widget == NULL || event_widget == child || ctk_widget_is_ancestor (event_widget, child))
                return FALSE;

        /* And also from the action widgets */
        action_widget = ctk_notebook_get_action_widget (notebook, CTK_PACK_START);
        if (event_widget == action_widget || (action_widget != NULL && ctk_widget_is_ancestor (event_widget, action_widget)))
                return FALSE;
        action_widget = ctk_notebook_get_action_widget (notebook, CTK_PACK_END);
        if (event_widget == action_widget || (action_widget != NULL && ctk_widget_is_ancestor (event_widget, action_widget)))
                return FALSE;

        switch (event->direction) {
        case CDK_SCROLL_RIGHT:
        case CDK_SCROLL_DOWN:
                ctk_notebook_next_page (notebook);
                break;
        case CDK_SCROLL_LEFT:
        case CDK_SCROLL_UP:
                ctk_notebook_prev_page (notebook);
                break;
        case CDK_SCROLL_SMOOTH:
                switch (ctk_notebook_get_tab_pos (notebook)) {
                case CTK_POS_LEFT:
                case CTK_POS_RIGHT:
                        if (event->delta_y > 0)
                                ctk_notebook_next_page (notebook);
                        else if (event->delta_y < 0)
                                ctk_notebook_prev_page (notebook);
                        break;
                case CTK_POS_TOP:
                case CTK_POS_BOTTOM:
                        if (event->delta_x > 0)
                                ctk_notebook_next_page (notebook);
                        else if (event->delta_x < 0)
                                ctk_notebook_prev_page (notebook);
                        break;
                }
                break;
        }

        return TRUE;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
cpm_common_test (gpointer data)
{
	EggTest *test = (EggTest *) data;
	if (egg_test_start (test, "CpmCommon") == FALSE)
		return;

	egg_test_end (test);
}

#endif

