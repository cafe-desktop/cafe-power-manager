/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
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

#ifndef CSD_MEDIA_KEYS_WINDOW_H
#define CSD_MEDIA_KEYS_WINDOW_H

#include <glib-object.h>
#include <ctk/ctk.h>

#include "csd-osd-window.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CSD_TYPE_MEDIA_KEYS_WINDOW            (csd_media_keys_window_get_type ())
#define CSD_MEDIA_KEYS_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj),  CSD_TYPE_MEDIA_KEYS_WINDOW, CsdMediaKeysWindow))
#define CSD_MEDIA_KEYS_WINDOW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),   CSD_TYPE_MEDIA_KEYS_WINDOW, CsdMediaKeysWindowClass))
#define CSD_IS_MEDIA_KEYS_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj),  CSD_TYPE_MEDIA_KEYS_WINDOW))
#define CSD_IS_MEDIA_KEYS_WINDOW_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS ((klass), CSD_TYPE_MEDIA_KEYS_WINDOW))

typedef struct CsdMediaKeysWindow                   CsdMediaKeysWindow;
typedef struct CsdMediaKeysWindowClass              CsdMediaKeysWindowClass;
typedef struct CsdMediaKeysWindowPrivate            CsdMediaKeysWindowPrivate;

struct CsdMediaKeysWindow {
        CsdOsdWindow parent;

        CsdMediaKeysWindowPrivate  *priv;
};

struct CsdMediaKeysWindowClass {
        CsdOsdWindowClass parent_class;
};

typedef enum {
        CSD_MEDIA_KEYS_WINDOW_ACTION_VOLUME,
        CSD_MEDIA_KEYS_WINDOW_ACTION_CUSTOM
} CsdMediaKeysWindowAction;

GType                 csd_media_keys_window_get_type          (void);

CtkWidget *           csd_media_keys_window_new               (void);
void                  csd_media_keys_window_set_action        (CsdMediaKeysWindow      *window,
                                                               CsdMediaKeysWindowAction action);
void                  csd_media_keys_window_set_action_custom (CsdMediaKeysWindow      *window,
                                                               const char              *icon_name,
                                                               gboolean                 show_level);
void                  csd_media_keys_window_set_volume_muted  (CsdMediaKeysWindow      *window,
                                                               gboolean                 muted);
void                  csd_media_keys_window_set_volume_level  (CsdMediaKeysWindow      *window,
                                                               int                      level);
gboolean              csd_media_keys_window_is_valid          (CsdMediaKeysWindow      *window);

#ifdef __cplusplus
}
#endif

#endif
