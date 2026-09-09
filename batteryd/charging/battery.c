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
 * @file battery.c
 *
 * @brief Battery interface calls to read the battery values.
 */

#include <string.h>
#include <syslog.h>
#include <stdbool.h>
#include <unistd.h>
#include <glib.h>
#include <json.h>
#include <luna-service2/lunaservice.h>

#include "init.h"
#include "batteryd_debug.h"
#include "logging.h"
#include "main.h"
#include "battery.h"
#include "charging_logic.h"
#include "batteryd_config.h"
#include "sysfs.h"

#define LOG_DOMAIN "BATTERY_IPC: "

static nyx_device_handle_t battDev = NULL;

nyx_battery_ctia_t battery_ctia_params;


/*
 * Returns false when there is nothing to read, having zeroed *status.
 *
 * This used to return void and, on both the no-device and the query-failed
 * path, leave the caller's struct exactly as it found it - uninitialised stack.
 * Every caller then read percentage, temperature and present out of it. The
 * battery poll state machine is driven by that "present", so a failed read
 * could put it in the removed state, and from there charging_logic shuts the
 * device down. Say so instead, and leave a determinate zeroed struct behind for
 * callers that carry on regardless.
 */
bool battery_read(nyx_battery_status_t *status)
{
    if (!status)
        return false;

    memset(status, 0, sizeof(*status));

    if(battDev == NULL)
        return false;

    nyx_error_t err = nyx_battery_query_battery_status(battDev,status);

    if(err != NYX_ERROR_NONE)
    {
        BATTERYDLOG(LOG_ERR,"%s: nyx_battery_query_battery_status returned with error : %d",__func__,err);
        memset(status, 0, sizeof(*status));
        return false;
    }

    return true;
}


int battery_get_ctia_params(void)
{
    if(!battDev)
        return -1;
    nyx_error_t err = nyx_battery_get_ctia_parameters(battDev,&battery_ctia_params);

    if(err != NYX_ERROR_NONE)
    {
        BATTERYDLOG(LOG_ERR,"%s: nyx_battery_get_charge_parameters returned with error : %d",__func__,err);
        return -1;
    }

    return 0;
}



bool battery_authenticate(void)
{
    bool result = false;
    nyx_error_t err;

    if (battery_ctia_params.skip_battery_authentication)
        return true;

    if (!battDev)
        return true;

    err = nyx_battery_authenticate_battery(battDev, &result);

    /*
     * The return value was ignored and an uninitialised bool returned with it.
     * A module that does not authenticate answers NYX_ERROR_NOT_IMPLEMENTED,
     * and most do not; treat anything we could not ask as authentic, because
     * the alternative is refusing to charge a battery we have no evidence
     * against.
     */
    if (err != NYX_ERROR_NONE)
    {
        BATTERYDLOG(LOG_DEBUG,"%s: nyx_battery_authenticate_battery returned %d,"
                    " treating the battery as authentic",__func__,err);
        return true;
    }

    return result;
}

void battery_set_wakeup_percentage(bool charging, bool suspend)
{
    int battlowpercent[] = {20,13,11,9,6,5,4,3,2,1,0};
    nyx_battery_status_t batt;
    int nextchk = 0,i = 0;

    if(!battDev)
        return;

    g_debug("battery_set_wakeup_percentage: calculating next wakeup point");
    battery_read(&batt);
    sendBatteryStatus();

    if(charging) {
        nextchk = 0;
    }
    else if(suspend) {
        for(i=0; battlowpercent[i]!=0; i++) {
            if(batt.percentage > battlowpercent[i])
            {
                nextchk=battlowpercent[i];
                break;
            }
        }
    }
    else
        nextchk=batt.percentage;

    BATTERYDLOG(LOG_DEBUG, "Setting percent limit to %d\n",nextchk);

    nyx_battery_set_wakeup_percentage(battDev, nextchk);
}


#define SYSFS_DEVICE "/sys/devices/w1 bus master/"

#define SYSFS_BATTERY_SEARCH          "w1_master_search"

const gchar *battery_search_file = SYSFS_DEVICE SYSFS_BATTERY_SEARCH;

void battery_search(bool on)
{
    g_debug("%s %s", __FUNCTION__, (on ? "On" : "Off"));

    if (on)
    {
        SysfsWriteString(battery_search_file, "-1");
    }
    else
    {
        SysfsWriteString(battery_search_file, "0");
    }
}


/**
* @brief Generate a fake battery percentage for the UI to consume.
*        This allows us to make 95-100% appear like 100%.
*
* @param  percent
*
* @retval
*/
static int getUiPercent(int percent)
{
    int min   =   0;
    int max   =  95;

    int x = percent;
    int range = max - min;

    if (x < min) x = min;
    else if (x > max) x = max;

    return (x - min) * 100 / (range);
}


/**
 * @brief Append the readings of one battery as a JSON object.
 */
static void appendBatteryObject(GString *buffer, const char *name,
                                const char *role, bool primary,
                                const nyx_battery_status_t *status)
{
    g_string_append_printf(buffer,
        "{\"name\":\"%s\",\"role\":\"%s\",\"primary\":%s,"
        "\"present\":%s,\"charging\":%s,"
        "\"percent\":%d,\"percent_ui\":%d,"
        "\"temperature_C\":%d,\"current_mA\":%d,\"voltage_mV\":%d,"
        "\"capacity_mAh\":%f}",
        name, role,
        primary ? "true" : "false",
        status->present ? "true" : "false",
        status->charging ? "true" : "false",
        status->percentage,
        getUiPercent(status->percentage),
        status->temperature,
        status->current,
        status->voltage,
        status->capacity);
}

/**
 * @brief Build the batteryStatus payload.
 *
 * The top level describes the primary battery and is exactly what it always
 * was, so every existing consumer keeps working. A device that has more than
 * one battery - a PinePhone (Pro) docked in its keyboard - additionally gets a
 * "batteries" array with one entry per battery, primary first. Devices with a
 * single battery, and modules built against a nyx that predates
 * nyx_battery_query_battery_count(), emit no array at all rather than a
 * one-element one restating the top level.
 *
 * Caller frees.
 */
static char *buildBatteryStatusPayload(void)
{
    nyx_battery_status_t status;
    int32_t count = 0;
    GString *buffer;

    if (!battDev)
        return NULL;

    nyx_error_t err = nyx_battery_query_battery_status(battDev,&status);

    if(err != NYX_ERROR_NONE)
    {
        BATTERYDLOG(LOG_ERR,"%s: nyx_battery_query_battery_status returned with error : %d",__func__,err);
        /* status is untouched stack; publishing it would broadcast garbage. */
        return NULL;
    }

    int percent_ui = getUiPercent(status.percentage);

    BATTERYDLOG(LOG_INFO,
            "(%fmAh, %d%%, %d%%_ui, %dC, %dmA, %dmV)\n",
            status.capacity, status.percentage,
            percent_ui,
            status.temperature,
            status.current, status.voltage);

    buffer = g_string_sized_new(500);
    g_string_append_printf(buffer,"{\"percent\":%d,\"percent_ui\":%d,"
                "\"temperature_C\":%d,\"current_mA\":%d,\"voltage_mV\":%d,"
                "\"capacity_mAh\":%f",
        status.percentage,
        percent_ui,
        status.temperature,
        status.current,
        status.voltage,
        status.capacity);

    /* NYX_ERROR_NOT_IMPLEMENTED here just means "one battery, the one above". */
    if (nyx_battery_query_battery_count(battDev, &count) == NYX_ERROR_NONE &&
        count > 1)
    {
        int32_t i;
        int32_t emitted = 0;

        g_string_append(buffer, ",\"batteries\":[");

        for (i = 0; i < count; i++)
        {
            nyx_battery_info_t info;

            if (nyx_battery_query_battery_info(battDev, i, &info) != NYX_ERROR_NONE)
                continue;

            if (emitted++ > 0)
                g_string_append_c(buffer, ',');

            appendBatteryObject(buffer, info.name, info.role, info.primary,
                                &info.status);
        }

        g_string_append_c(buffer, ']');
    }

    g_string_append_c(buffer, '}');

    return g_string_free(buffer, FALSE);
}

bool batteryStatusQuery(LSHandle *sh,
                   LSMessage *message, void *user_data)
{
    char *payload = buildBatteryStatusPayload();

    if (!payload)
        return false;

    BATTERYDLOG(LOG_DEBUG,"%s: Sending payload : %s",__func__,payload);
    LSError lserror;
    LSErrorInit(&lserror);
    bool retVal = LSMessageReply(sh, message, payload,NULL);
    if (!retVal)
    {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }
    g_free(payload);
    return TRUE;
}

void sendBatteryStatus(void)
{
    char *payload = buildBatteryStatusPayload();

    if (!payload)
        return;

    BATTERYDLOG(LOG_DEBUG,"%s: Sending payload : %s",__func__,payload);
    LSError lserror;
    LSErrorInit(&lserror);
    bool retVal = LSSignalSend(GetLunaServiceHandle(),
        "luna://com.webos.service.battery/com/palm/power/batteryStatus",
        payload, &lserror);
    if (!retVal)
    {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    g_free(payload);
    return;
}


void notifyBatteryStatus(nyx_device_handle_t handle, nyx_callback_status_t status, void* data)
{
    sendBatteryStatus();

    /* new readings are the only chance a discharging device gets to notice
     * that it has run out */
    BatteryLevelCheck();
}

bool batteryStatusQuerySignal(LSHandle *sh,
                   LSMessage *message, void *user_data)
{
    sendBatteryStatus();
    return true;
}

static
bool BatteryDummyValues(int percent, int temp_C, int current_mA, int voltage_mV, float capacity_mAh)
{
    #define FAKEBATT    "/tmp/fakebattery/"

    static const struct { const char *node; const char *fmt; } nodes[] = {
        { "percentage",  "%d" },
        { "temperature", "%d" },
        { "current",     "%d" },
        { "voltage",     "%d" },
    };
    char value[256];
    size_t i;
    int ints[4];

    ints[0] = percent;
    ints[1] = temp_C;
    ints[2] = current_mA;
    ints[3] = voltage_mV;

    /*
     * This used to shell out to "mkdir ...; touch ...; touch ..." with
     * system(), six times over, because SysfsWriteString opens O_WRONLY and
     * cannot create. g_file_set_contents creates and replaces, so neither the
     * shell nor the touches are needed.
     */
    if (g_mkdir_with_parents(FAKEBATT, 0755) != 0)
    {
        BATTERYDLOG(LOG_ERR,"%s: cannot create %s",__func__,FAKEBATT);
        return false;
    }

    for (i = 0; i < G_N_ELEMENTS(nodes); i++)
    {
        gchar *path = g_build_filename(FAKEBATT, nodes[i].node, NULL);
        gboolean ok;

        snprintf(value, sizeof(value), nodes[i].fmt, ints[i]);
        ok = g_file_set_contents(path, value, -1, NULL);
        g_free(path);

        if (!ok)
            return false;
    }

    snprintf(value, sizeof(value), "%8.3f", capacity_mAh);

    return g_file_set_contents(FAKEBATT "capacity", value, -1, NULL) != FALSE;
}


bool
fakeBatteryStatus(LSHandle *sh,
                   LSMessage *message, void *user_data)
{
    /* Ignore the successful registration. */
    if (strcmp(LSMessageGetMethod(message), LUNABUS_SIGNAL_REGISTERED) == 0)
    {
         return true;
    }

    const char *payload = LSMessageGetPayload(message);
    struct json_object *object = json_tokener_parse(payload);
    if (!object) {
        goto end;
    }

    int percent; int temp_C; int current_mA; int voltage_mV;
    float capacity_mAh;

    percent = json_object_get_int(
            json_object_object_get(object, "percent"));

    temp_C = json_object_get_int(
            json_object_object_get(object, "temperature_C"));

    current_mA = json_object_get_int(
            json_object_object_get(object, "current_mA"));

    voltage_mV = json_object_get_int(
            json_object_object_get(object, "voltage_mV"));

    capacity_mAh = json_object_get_double(
            json_object_object_get(object, "capacity_mAh"));

    if(!BatteryDummyValues(percent,temp_C,current_mA,voltage_mV,capacity_mAh))
    {
        BATTERYDLOG(LOG_ERR,"Unable to load fake battery values");
    }

    g_debug("%s %f mAh, P: %d%%, T: %d C, C: %d mA, V: %d mV",
        __FUNCTION__,capacity_mAh,percent,temp_C,
        current_mA, voltage_mV);

end:
    if (object) json_object_put(object);
    return true;
}


/**
 * @brief Initialize tha NYX api for charging and send the batteryd config parameters.
 */

int BatteryInit(void)
{
    int ret = 0;
    nyx_error_t error = NYX_ERROR_NONE;
    nyx_device_iterator_handle_t iterator = NULL;

    error = nyx_device_get_iterator(NYX_DEVICE_BATTERY, NYX_FILTER_DEFAULT, &iterator);
    if(error != NYX_ERROR_NONE || iterator == NULL) {
         goto error;
    }
    else if (error == NYX_ERROR_NONE)
    {
        nyx_device_id_t id = NULL;

        while ((error = nyx_device_iterator_get_next_id(iterator,
            &id)) == NYX_ERROR_NONE && NULL != id)
        {
            g_debug("Batteryd: Battery device id \"%s\" found",id);
            error = nyx_device_open(NYX_DEVICE_BATTERY, id, &battDev);
            if(error != NYX_ERROR_NONE)
            {
                goto error;
            }
            break;
        }
    }

    LSError lserror;
    LSErrorInit(&lserror);
    bool retVal;
    retVal = LSCall(GetLunaServiceHandle(),
        "luna://com.palm.lunabus/signal/addmatch",
            "{\"category\":\"/com/palm/power\","
             "\"method\":\"batteryStatusQuery\"}",
             batteryStatusQuerySignal,
             NULL, NULL, &lserror);
    if (!retVal)
        goto lserror;

    if (gChargeConfig.fake_battery)
    {
        retVal = LSCall(GetLunaServiceHandle(),
            "luna://com.palm.lunabus/signal/addmatch",
                "{\"category\":\"/com/palm/power\","
                 "\"method\":\"fakeBatteryStatus\"}",
                 fakeBatteryStatus,
                 NULL, NULL, &lserror);
        if (!retVal) goto lserror;
    }

    nyx_battery_register_battery_status_callback(battDev,notifyBatteryStatus,NULL);

    /*
     * Say what we are working with. A device that turns out to have two
     * batteries when it should have one, or a keyboard battery that never
     * shows up, is otherwise only visible by reading the broadcast payload.
     */
    {
        int32_t count = 0;

        if (nyx_battery_query_battery_count(battDev, &count) == NYX_ERROR_NONE)
        {
            int32_t i;

            BATTERYDLOG(LOG_INFO,"Batteryd: %d batter%s reported by nyx",
                        count, (1 == count) ? "y" : "ies");

            for (i = 0; i < count; i++)
            {
                nyx_battery_info_t info;

                if (nyx_battery_query_battery_info(battDev, i, &info) == NYX_ERROR_NONE)
                    BATTERYDLOG(LOG_INFO,"Batteryd:   [%d] %s (%s)%s%s",
                                i, info.name, info.role,
                                info.primary ? ", primary" : "",
                                info.status.present ? "" : ", not present");
            }
        }
    }

out:
    if(iterator)
        nyx_device_release_iterator(iterator);
    return ret;

lserror:
    LSErrorPrint (&lserror, stderr);
    LSErrorFree (&lserror);
    ret = -1;
    goto out;

error:
    g_critical("Batteryd: No battery device found\n");
    battDev = NULL;
    if(iterator)
        nyx_device_release_iterator(iterator);
//    abort();
    return 0;
}

INIT_FUNC(INIT_FUNC_FIRST, BatteryInit);

