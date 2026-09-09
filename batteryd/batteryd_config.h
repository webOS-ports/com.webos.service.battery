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


#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <stdbool.h>

/**
 * @brief The charge config structure defining various charger parameters for
 * the charger module.
 */

typedef struct chargeConfig
{
    bool debug;

    bool skip_battery_check;
    bool disable_overcharge_check;
    bool fake_battery;
    bool disable_charging;
    bool skip_battery_authentication;

    const char *preference_dir;

    int fasthalt;
    int maxtemp;
    int temprate;

    /**
     * Shut down at or below this battery percentage while nothing is charging.
     * 0 disables the check, leaving only the voltage and temperature triggers.
     */
    int critical_percent;
}chargeConfig_t;

extern chargeConfig_t gChargeConfig;

int config_init(void);

#endif // _CONFIG_H_
