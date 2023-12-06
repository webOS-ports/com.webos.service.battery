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


/**
 * @file com_webos_service_battery_lunabus.c
 *
 * @brief Register lunabus methods and signals for batteryd.
 */

#include <luna-service2/lunaservice.h>
#include "main.h"
#include "init.h"

#define DECLARE_LSMETHOD(methodName) \
    bool methodName(LSHandle *handle, LSMessage *message, void *user_data)

DECLARE_LSMETHOD(identifyCallback);

DECLARE_LSMETHOD(batteryStatusQuery);
DECLARE_LSMETHOD(chargerStatusQuery);

DECLARE_LSMETHOD(TESTChargeStateFault);
DECLARE_LSMETHOD(TESTChargeStateShutdown);

LSMethod com_webos_service_battery_methods[] = {
    { "batteryStatusQuery", batteryStatusQuery },
    { "chargerStatusQuery", chargerStatusQuery },
    { },
};

LSSignal com_webos_service_battery_signals[] = {
    { "batteryStatus" },
    { "batteryStatusQuery" },
    { "chargerStatus" },
    { "chargerStatusQuery" },

    { "chargerConnected" },
    { "USBDockStatus" },
    
    { },
};

static int
com_webos_service_battery_lunabus_init(void)
{
    LSError lserror;
    LSErrorInit(&lserror);

    if (!LSRegisterCategory(GetLunaServiceHandle(), "/com/palm/power",
        com_webos_service_battery_methods, com_webos_service_battery_signals,
        NULL, &lserror))
    {
        goto error;
    }
    return 0;

error:
    LSErrorPrint(&lserror, stderr);
    LSErrorFree(&lserror);
    return -1;
}

INIT_FUNC(INIT_FUNC_END, com_webos_service_battery_lunabus_init);
