/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2007 Richard Hughes <richard@hughsie.com>
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

#ifndef __CPMBUTTON_H
#define __CPMBUTTON_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CPM_TYPE_BUTTON		(cpm_button_get_type ())
#define CPM_BUTTON(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), CPM_TYPE_BUTTON, CpmButton))
#define CPM_BUTTON_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), CPM_TYPE_BUTTON, CpmButtonClass))
#define CPM_IS_BUTTON(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), CPM_TYPE_BUTTON))
#define CPM_IS_BUTTON_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), CPM_TYPE_BUTTON))
#define CPM_BUTTON_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), CPM_TYPE_BUTTON, CpmButtonClass))

typedef struct CpmButtonPrivate CpmButtonPrivate;

#define CPM_BUTTON_POWER		"power"
#define CPM_BUTTON_SLEEP		"sleep"
#define CPM_BUTTON_SUSPEND		"suspend"
#define CPM_BUTTON_HIBERNATE		"hibernate"
#define CPM_BUTTON_LID_DEP		"lid"		/* Remove when HAL drops input support */
#define CPM_BUTTON_LID_OPEN		"lid-up"
#define CPM_BUTTON_LID_CLOSED		"lid-down"
#define CPM_BUTTON_BRIGHT_UP		"brightness-up"
#define CPM_BUTTON_BRIGHT_DOWN		"brightness-down"
#define CPM_BUTTON_KBD_BRIGHT_UP	"kbd-illum-up"
#define CPM_BUTTON_KBD_BRIGHT_DOWN	"kbd-illum-down"
#define CPM_BUTTON_KBD_BRIGHT_TOGGLE	"kbd-illum-toggle"
#define CPM_BUTTON_LOCK			"lock"
#define CPM_BUTTON_BATTERY		"battery"

typedef struct
{
	GObject		 parent;
	CpmButtonPrivate *priv;
} CpmButton;

typedef struct
{
	GObjectClass	parent_class;
	void		(* button_pressed)	(CpmButton	*button,
						 const gchar	*type);
} CpmButtonClass;

GType		 cpm_button_get_type		(void);
CpmButton	*cpm_button_new			(void);
gboolean	 cpm_button_is_lid_closed	(CpmButton *button);
gboolean	 cpm_button_reset_time		(CpmButton *button);

G_END_DECLS

#endif	/* __CPMBUTTON_H */
