/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * On-screen-display (OSD) window for cafe-settings-daemon's plugins
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2009 Novell, Inc
 *
 * Authors:
 *   William Jon McCann <mccann@jhu.edu>
 *   Federico Mena-Quintero <federico@novell.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

/* CsdOsdWindow is an "on-screen-display" window (OSD).  It is the cute,
 * semi-transparent, curved popup that appears when you press a hotkey global to
 * the desktop, such as to change the volume, switch your monitor's parameters,
 * etc.
 *
 * You can create a CsdOsdWindow and use it as a normal CtkWindow.  It will
 * automatically center itself, figure out if it needs to be composited, etc.
 * Just pack your widgets in it, sit back, and enjoy the ride.
 */

#ifndef CSD_OSD_WINDOW_H
#define CSD_OSD_WINDOW_H

#include <glib-object.h>
#include <ctk/ctk.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Alpha value to be used for foreground objects drawn in an OSD window */
#define CSD_OSD_WINDOW_FG_ALPHA 1.0

#define CSD_TYPE_OSD_WINDOW            (csd_osd_window_get_type ())
#define CSD_OSD_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj),  CSD_TYPE_OSD_WINDOW, CsdOsdWindow))
#define CSD_OSD_WINDOW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),   CSD_TYPE_OSD_WINDOW, CsdOsdWindowClass))
#define CSD_IS_OSD_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj),  CSD_TYPE_OSD_WINDOW))
#define CSD_IS_OSD_WINDOW_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS ((klass), CSD_TYPE_OSD_WINDOW))
#define CSD_OSD_WINDOW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CSD_TYPE_OSD_WINDOW, CsdOsdWindowClass))

typedef struct CsdOsdWindow                   CsdOsdWindow;
typedef struct CsdOsdWindowClass              CsdOsdWindowClass;
typedef struct CsdOsdWindowPrivate            CsdOsdWindowPrivate;

struct CsdOsdWindow {
        CtkWindow                   parent;

        CsdOsdWindowPrivate  *priv;
};

struct CsdOsdWindowClass {
        CtkWindowClass parent_class;

        void (* draw_when_composited) (CsdOsdWindow *window, cairo_t *cr);
};

GType                 csd_osd_window_get_type          (void);

CtkWidget *           csd_osd_window_new               (void);
gboolean              csd_osd_window_is_composited     (CsdOsdWindow      *window);
gboolean              csd_osd_window_is_valid          (CsdOsdWindow      *window);
void                  csd_osd_window_update_and_hide   (CsdOsdWindow      *window);

#ifdef __cplusplus
}
#endif

#endif
