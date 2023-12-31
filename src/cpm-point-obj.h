/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __CPM_POINT_OBJ_H__
#define __CPM_POINT_OBJ_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct
{
	gfloat		 x;
	gfloat		 y;
	guint32		 color;
} CpmPointObj;

CpmPointObj	*cpm_point_obj_new		(void);
CpmPointObj	*cpm_point_obj_copy		(const CpmPointObj	*cobj);
void		 cpm_point_obj_free		(CpmPointObj		*obj);

G_END_DECLS

#endif /* __CPM_POINT_OBJ_H__ */

