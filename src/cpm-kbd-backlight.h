/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Alex Launi <alex launi canonical com>
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

#ifndef __CPM_KBD_BACKLIGHT_H
#define __CPM_KBD_BACKLIGHT_H

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define CPM_TYPE_KBD_BACKLIGHT     (cpm_kbd_backlight_get_type ())
#define CPM_KBD_BACKLIGHT(o)       (G_TYPE_CHECK_INSTANCE_CAST ((o), CPM_TYPE_KBD_BACKLIGHT, GpmKbdBacklight))
#define CPM_KBD_BACKLIGHT_CLASS(k) (G_TYPE_CHECK_CLASS_CAST((k), CPM_TYPE_KBD_BACKLIGHT, GpmKbdBacklightClass))
#define CPM_IS_KBD_BACKLIGHT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CPM_TYPE_KBD_BACKLIGHT))
#define CPM_IS_KBD_BACKLIGHT_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CPM_TYPE_KBD_BACKLIGHT))
#define CPM_KBD_BACKLIGHT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CPM_TYPE_KBD_BACKLIGHT, GpmKbdBacklightClass))

#define CPM_KBD_BACKLIGHT_DIM_INTERVAL 5 /* ms */
#define CPM_KBD_BACKLIGHT_STEP 10 /* change by 10% each step */

typedef struct GpmKbdBacklightPrivate GpmKbdBacklightPrivate;

typedef struct
{
   GObject         parent;
   GpmKbdBacklightPrivate *priv;
} GpmKbdBacklight;

typedef struct
{
   GObjectClass parent_class;
   void         (* brightness_changed) (GpmKbdBacklight *backlight,
                        gint         brightness);
} GpmKbdBacklightClass;

typedef enum
{
    CPM_KBD_BACKLIGHT_ERROR_GENERAL,
    CPM_KBD_BACKLIGHT_ERROR_DATA_NOT_AVAILABLE,
    CPM_KBD_BACKLIGHT_ERROR_HARDWARE_NOT_PRESENT
} GpmKbdBacklightError;

GType          cpm_kbd_backlight_get_type      (void);
GQuark         cpm_kbd_backlight_error_quark       (void);
GpmKbdBacklight           *cpm_kbd_backlight_new           (void);
gboolean       cpm_kbd_backlight_get_brightness    (GpmKbdBacklight *backlight,
                                guint *brightness,
                                GError **error);
gboolean       cpm_kbd_backlight_set_brightness    (GpmKbdBacklight *backlight,
                                guint brightness,
                                GError **error);
void           cpm_kbd_backlight_register_dbus     (GpmKbdBacklight *backlight,
                                GDBusConnection *connection,
                                GError **error);

G_END_DECLS

#endif /* __CPM_KBD_BACKLIGHT_H */

