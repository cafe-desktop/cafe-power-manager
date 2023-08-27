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

#ifndef __CPMCOMMON_H
#define __CPMCOMMON_H

#include <glib.h>
#include <ctk/ctk.h>

#include <unistd.h>

G_BEGIN_DECLS

#define LOGIND_RUNNING() (access("/run/systemd/seats/", F_OK) >= 0)

#define	CPM_DBUS_SERVICE		"org.cafe.PowerManager"
#define	CPM_DBUS_INTERFACE		"org.cafe.PowerManager"
#define	CPM_DBUS_INTERFACE_BACKLIGHT	"org.cafe.PowerManager.Backlight"
#define	CPM_DBUS_INTERFACE_KBD_BACKLIGHT	"org.cafe.PowerManager.KbdBacklight"
#define	CPM_DBUS_PATH			"/org/cafe/PowerManager"
#define	CPM_DBUS_PATH_BACKLIGHT		"/org/cafe/PowerManager/Backlight"
#define CPM_DBUS_PATH_KBD_BACKLIGHT    "/org/cafe/PowerManager/KbdBacklight"

/* common descriptions of this program */
#define CPM_NAME 			_("Power Manager")
#define CPM_DESCRIPTION 		_("Power Manager for the CAFE desktop")

/* schema location */
#define CPM_SETTINGS_SCHEMA				"org.cafe.power-manager"

/* actions */
#define CPM_SETTINGS_ACTION_CRITICAL_UPS		"action-critical-ups"
#define CPM_SETTINGS_ACTION_CRITICAL_BATT		"action-critical-battery"
#define CPM_SETTINGS_ACTION_LOW_UPS			"action-low-ups"
#define CPM_SETTINGS_ACTION_SLEEP_TYPE_AC		"action-sleep-type-ac"
#define CPM_SETTINGS_ACTION_SLEEP_TYPE_BATT		"action-sleep-type-battery"
#define CPM_SETTINGS_SLEEP_WHEN_CLOSED			"event-when-closed-battery"

/* backlight stuff */
#define CPM_SETTINGS_BACKLIGHT_ENABLE			"backlight-enable"
#define CPM_SETTINGS_BACKLIGHT_BATTERY_REDUCE		"backlight-battery-reduce"
#define CPM_SETTINGS_DPMS_METHOD_AC			"dpms-method-ac"
#define CPM_SETTINGS_DPMS_METHOD_BATT			"dpms-method-battery"
#define CPM_SETTINGS_IDLE_BRIGHTNESS			"idle-brightness"
#define CPM_SETTINGS_IDLE_DIM_AC			"idle-dim-ac"
#define CPM_SETTINGS_IDLE_DIM_BATT			"idle-dim-battery"
#define CPM_SETTINGS_IDLE_DIM_TIME			"idle-dim-time"
#define CPM_SETTINGS_BRIGHTNESS_AC			"brightness-ac"
#define CPM_SETTINGS_BRIGHTNESS_DIM_BATT		"brightness-dim-battery"

/* keyboard backlight */
#define CPM_SETTINGS_KBD_BACKLIGHT_BATT_REDUCE     "kbd-backlight-battery-reduce"
#define CPM_SETTINGS_KBD_BRIGHTNESS_ON_AC      "kbd-brightness-on-ac"
#define CPM_SETTINGS_KBD_BRIGHTNESS_DIM_BY_ON_BATT      "kbd-brightness-dim-by-on-battery"
#define CPM_SETTINGS_KBD_BRIGHTNESS_DIM_BY_ON_IDLE "kbd-brightness-dim-by-on-idle"

/* buttons */
#define CPM_SETTINGS_BUTTON_LID_AC			"button-lid-ac"
#define CPM_SETTINGS_BUTTON_LID_BATT			"button-lid-battery"
#define CPM_SETTINGS_BUTTON_SUSPEND			"button-suspend"
#define CPM_SETTINGS_BUTTON_HIBERNATE			"button-hibernate"
#define CPM_SETTINGS_BUTTON_POWER			"button-power"

/* general */
#define CPM_SETTINGS_USE_TIME_POLICY			"use-time-for-policy"
#define CPM_SETTINGS_NETWORKMANAGER_SLEEP		"network-sleep"
#define CPM_SETTINGS_IDLE_CHECK_CPU			"check-type-cpu"

/* lock */
#define CPM_SETTINGS_LOCK_USE_SCREENSAVER		"lock-use-screensaver"
#define CPM_SETTINGS_LOCK_ON_BLANK_SCREEN		"lock-blank-screen"
#define CPM_SETTINGS_LOCK_ON_SUSPEND			"lock-suspend"
#define CPM_SETTINGS_LOCK_ON_HIBERNATE			"lock-hibernate"
#define CPM_SETTINGS_LOCK_KEYRING_SUSPEND		"lock-keyring-suspend"
#define CPM_SETTINGS_LOCK_KEYRING_HIBERNATE		"lock-keyring-hibernate"

/* notify */
#define CPM_SETTINGS_NOTIFY_LOW_CAPACITY		"notify-low-capacity"
#define CPM_SETTINGS_NOTIFY_DISCHARGING			"notify-discharging"
#define CPM_SETTINGS_NOTIFY_FULLY_CHARGED		"notify-fully-charged"
#define CPM_SETTINGS_NOTIFY_SLEEP_FAILED		"notify-sleep-failed"
#define CPM_SETTINGS_NOTIFY_SLEEP_FAILED_URI		"notify-sleep-failed-uri"
#define CPM_SETTINGS_NOTIFY_LOW_POWER			"notify-low-power"
#define CPM_SETTINGS_NOTIFY_LOW_CAPACITY_MOUSE		"notify-low-capacity-mouse"

/* thresholds */
#define CPM_SETTINGS_PERCENTAGE_LOW			"percentage-low"
#define CPM_SETTINGS_PERCENTAGE_CRITICAL		"percentage-critical"
#define CPM_SETTINGS_PERCENTAGE_ACTION			"percentage-action"
#define CPM_SETTINGS_TIME_LOW				"time-low"
#define CPM_SETTINGS_TIME_CRITICAL			"time-critical"
#define CPM_SETTINGS_TIME_ACTION			"time-action"

/* timeout */
#define CPM_SETTINGS_SLEEP_COMPUTER_AC			"sleep-computer-ac"
#define CPM_SETTINGS_SLEEP_COMPUTER_BATT		"sleep-computer-battery"
#define CPM_SETTINGS_SLEEP_COMPUTER_UPS			"sleep-computer-ups"
#define CPM_SETTINGS_SLEEP_DISPLAY_AC			"sleep-display-ac"
#define CPM_SETTINGS_SLEEP_DISPLAY_BATT			"sleep-display-battery"
#define CPM_SETTINGS_SLEEP_DISPLAY_UPS			"sleep-display-ups"

/* ui */
#define CPM_SETTINGS_ICON_POLICY			"icon-policy"
#define CPM_SETTINGS_ENABLE_SOUND			"enable-sound"
#define CPM_SETTINGS_SHOW_ACTIONS			"show-actions"

/* statistics */
#define CPM_SETTINGS_INFO_HISTORY_TIME			"info-history-time"
#define CPM_SETTINGS_INFO_HISTORY_TYPE			"info-history-type"
#define CPM_SETTINGS_INFO_HISTORY_GRAPH_SMOOTH		"info-history-graph-smooth"
#define CPM_SETTINGS_INFO_HISTORY_GRAPH_POINTS		"info-history-graph-points"
#define CPM_SETTINGS_INFO_STATS_TYPE			"info-stats-type"
#define CPM_SETTINGS_INFO_STATS_GRAPH_SMOOTH		"info-stats-graph-smooth"
#define CPM_SETTINGS_INFO_STATS_GRAPH_POINTS		"info-stats-graph-points"
#define CPM_SETTINGS_INFO_PAGE_NUMBER			"info-page-number"
#define CPM_SETTINGS_INFO_LAST_DEVICE			"info-last-device"

/* cafe-screensaver */
#define GS_SETTINGS_SCHEMA				"org.cafe.screensaver"
#define GS_SETTINGS_PREF_LOCK_ENABLED			"lock-enabled"

typedef enum {
	CPM_ICON_POLICY_ALWAYS,
	CPM_ICON_POLICY_PRESENT,
	CPM_ICON_POLICY_CHARGE,
	CPM_ICON_POLICY_LOW,
	CPM_ICON_POLICY_CRITICAL,
	CPM_ICON_POLICY_NEVER
} CpmIconPolicy;

typedef enum {
	CPM_ACTION_POLICY_BLANK,
	CPM_ACTION_POLICY_SUSPEND,
	CPM_ACTION_POLICY_SHUTDOWN,
	CPM_ACTION_POLICY_HIBERNATE,
	CPM_ACTION_POLICY_INTERACTIVE,
	CPM_ACTION_POLICY_NOTHING
} CpmActionPolicy;

gchar        *cpm_get_timestring                (guint time);
guint        cpm_discrete_from_percent          (guint percentage,
                                                 guint levels);
guint        cpm_discrete_to_percent            (guint discrete,
                                                 guint levels);
void         cpm_help_display                   (const gchar    *link_id);
gboolean     cpm_dialog_page_scroll_event_cb    (CtkWidget      *widget,
                                                 CdkEventScroll *event,
                                                 CtkWindow      *window);
#ifdef EGG_TEST
void         cpm_common_test                     (gpointer data);
#endif

G_END_DECLS

#endif	/* __CPMCOMMON_H */
