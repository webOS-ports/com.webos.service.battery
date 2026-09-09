/* @@@LICENSE
*
*      Copyright (c) 2007-2013 LG Electronics, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */


#include <stdbool.h>
#include <luna-service2/lunaservice.h>

#include <nyx/nyx_client.h>

#ifndef __BATTERY_H__
#define __BATTERY_H__

/**
 * Structures
 */

/*
 * Defined in battery.c. Both battery.c and charging_logic.c used to carry a
 * tentative definition of their own, which is a duplicate-symbol link failure
 * under -fno-common - the compiler default since GCC 10 - and only ever linked
 * because something in the build was still passing -fcommon.
 */
extern nyx_battery_ctia_t battery_ctia_params;

void BatteryCheckReason(int batterycheck);

bool BatteryIsPresent(void);
bool BatteryIsAuthentic(void);

bool battery_authenticate(void);

bool battery_read(nyx_battery_status_t *state);

void battery_search(bool on);

int battery_get_ctia_params(void);
void battery_set_wakeup_percentage(bool charging, bool suspend);



/**
 * Lunabus callbacks.
 */

bool batteryStatusQuery(LSHandle *sh, LSMessage *message, void *user_data);

/**
 * Lunabus signals
 */

void sendBatteryStatus(void);

#endif // __BATTERY_H__
