/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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

#ifndef __CPMPHONE_H
#define __CPMPHONE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CPM_TYPE_PHONE		(cpm_phone_get_type ())
#define CPM_PHONE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), CPM_TYPE_PHONE, CpmPhone))
#define CPM_PHONE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), CPM_TYPE_PHONE, CpmPhoneClass))
#define CPM_IS_PHONE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), CPM_TYPE_PHONE))
#define CPM_IS_PHONE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), CPM_TYPE_PHONE))
#define CPM_PHONE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), CPM_TYPE_PHONE, CpmPhoneClass))

#define CAFE_PHONE_MANAGER_DBUS_SERVICE	"org.gnome.phone"
#define CAFE_PHONE_MANAGER_DBUS_PATH		"/org/gnome/phone/Manager"
#define CAFE_PHONE_MANAGER_DBUS_INTERFACE	"org.gnome.phone.Manager"

typedef struct CpmPhonePrivate CpmPhonePrivate;

typedef struct
{
	GObject		       parent;
	CpmPhonePrivate *priv;
} CpmPhone;

typedef struct
{
	GObjectClass	parent_class;
	void		(* device_added)		(CpmPhone	*phone,
							 guint		 idx);
	void		(* device_removed)		(CpmPhone	*phone,
							 guint		 idx);
	void		(* device_refresh)		(CpmPhone	*phone,
							 guint		 idx);
} CpmPhoneClass;

GType		 cpm_phone_get_type			(void);
CpmPhone	*cpm_phone_new				(void);

gboolean	 cpm_phone_get_present			(CpmPhone	*phone,
							 guint		 idx);
guint		 cpm_phone_get_percentage		(CpmPhone	*phone,
							 guint		 idx);
gboolean	 cpm_phone_get_on_ac			(CpmPhone	*phone,
							 guint		 idx);
guint		 cpm_phone_get_num_batteries		(CpmPhone	*phone);
gboolean	 cpm_phone_coldplug			(CpmPhone	*phone);
#ifdef EGG_TEST
void		 cpm_phone_test				(gpointer	 data);
#endif

G_END_DECLS

#endif	/* __CPMPHONE_H */
