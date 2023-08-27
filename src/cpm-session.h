/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

#ifndef __CPM_SESSION_H
#define __CPM_SESSION_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CPM_TYPE_SESSION		(cpm_session_get_type ())
#define CPM_SESSION(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), CPM_TYPE_SESSION, CpmSession))
#define CPM_SESSION_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), CPM_TYPE_SESSION, CpmSessionClass))
#define CPM_IS_SESSION(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), CPM_TYPE_SESSION))
#define CPM_IS_SESSION_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), CPM_TYPE_SESSION))
#define CPM_SESSION_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), CPM_TYPE_SESSION, CpmSessionClass))

typedef struct CpmSessionPrivate CpmSessionPrivate;

typedef struct
{
	GObject			 parent;
	CpmSessionPrivate	*priv;
} CpmSession;

typedef struct
{
	GObjectClass	parent_class;
	void		(* idle_changed)		(CpmSession	*session,
							 gboolean	 is_idle);
	void		(* inhibited_changed)		(CpmSession	*session,
							 gboolean	 is_idle_inhibited,
							 gboolean        is_suspend_inhibited);
	/* just exit */
	void		(* stop)			(CpmSession	*session);
	/* reply with EndSessionResponse */
	void		(* query_end_session)		(CpmSession	*session,
							 guint		 flags);
	/* reply with EndSessionResponse */
	void		(* end_session)			(CpmSession	*session,
							 guint		 flags);
	void		(* cancel_end_session)		(CpmSession	*session);
} CpmSessionClass;

GType		 cpm_session_get_type			(void);
CpmSession	*cpm_session_new			(void);

gboolean	 cpm_session_logout			(CpmSession	*session);
gboolean	 cpm_session_get_idle			(CpmSession	*session);
gboolean	 cpm_session_get_idle_inhibited		(CpmSession	*session);
gboolean	 cpm_session_get_suspend_inhibited	(CpmSession	*session);
gboolean	 cpm_session_register_client		(CpmSession	*session,
							 const gchar	*app_id,
							 const gchar	*client_startup_id);
gboolean	 cpm_session_end_session_response	(CpmSession	*session,
							 gboolean	 is_okay,
							 const gchar	*reason);

G_END_DECLS

#endif	/* __CPM_SESSION_H */
