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
 * @file charging.c
 *
 * @brief A very minimal charging implementation just for letting the world know about the charging status.
 *
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
#include "batterypoll.h"
#include "charging_logic.h"
#include "batteryd_config.h"
#include "charger_eval.h"

#define LOG_DOMAIN "CHG: "

#include <nyx/nyx_client.h>

/* How often to look at the charger even though nyx has said nothing. */
#define CHARGER_RESYNC_SECONDS 30

static nyx_device_handle_t nyxDev = NULL;

/* What nyx said the last time anyone read it: answers ChargerIs*(). */
static nyx_charger_status_t currStatus;

/*
 * What the bus was last told. Broadcast decisions compare against this and
 * only this, so a read that did not end in a broadcast (a chargerStatusQuery
 * reply, a periodic check that found nothing new) cannot make a later real
 * change look like "no change".
 */
static charger_view_t lastBroadcast;
static bool lastBroadcastValid = false;

static bool ChargerBroadcast(const nyx_charger_status_t *status, bool force);

const char *
ChargerNameToString(int type)
{
    if(type & NYX_CHARGER_PC_CONNECTED)
        return "pc";
    else if(type & NYX_CHARGER_WALL_CONNECTED)
        return "wall";
    else if(type & NYX_CHARGER_DIRECT_CONNECTED)
        return "direct";
    else
        return "none";
}

/**
 * @brief Convert charger type enum to string
 */

const char *
ChargerTypeToString(int type)
{
     if(type & NYX_CHARGER_USB_POWERED)
        return "usb";
     else if(type & NYX_CHARGER_INDUCTIVE_POWERED)
        return "inductive";
     else
        return "none";
}

bool ChargerIsConnected(void)
{
    charger_view_t v = charger_view(&currStatus);
    return v.any;
}

/*
 * Read the charger from nyx into *status and remember it. Returns false when
 * nyx could not answer, leaving *status zeroed.
 */
static bool
ChargerRead(nyx_charger_status_t *status, const char *why)
{
    nyx_error_t err;

    memset(status, 0, sizeof(*status));

    if (!nyxDev)
        return false;

    err = nyx_charger_query_charger_status(nyxDev, status);

    if (err != NYX_ERROR_NONE)
    {
        BATTERYDLOG(LOG_ERR,"%s (%s): nyx_charger_query_charger_status returned with error : %d",
                    __func__, why, err);
        memset(status, 0, sizeof(*status));
        return false;
    }

    memcpy(&currStatus, status, sizeof(currStatus));
    return true;
}

static char *
ChargerDockStatusPayload(const nyx_charger_status_t *status,
                         const charger_view_t *v, bool with_connected)
{
    return g_strdup_printf("{\"DockConnected\":%s,\"DockPower\":%s,\"DockSerialNo\":\"%s\","
                "\"USBConnected\":%s,\"USBName\":\"%s\",\"Charging\":%s%s%s}",
                v->dock ? "true" : "false",
                (status->powered & NYX_CHARGER_INDUCTIVE_POWERED) ? "true" :"false",
                (strlen(status->dock_serial_number)) ? status->dock_serial_number : "NULL",
                v->wired ? "true" : "false",
                ChargerNameToString(status->connected),
                v->charging ? "true":"false",
                with_connected ? ",\"connected\":" : "",
                with_connected ? (v->any ? "true" : "false") : "");
}

bool ChargerIsCharging(void)
{
    return currStatus.is_charging;
}

bool
chargerStatusQuery(LSHandle *sh,
                   LSMessage *message, void *user_data)
{
    nyx_charger_status_t status;
    charger_view_t v;

    if (!ChargerRead(&status, "query"))
    {
        /* status is untouched stack; answering out of it reports garbage. */
        return false;
    }

    v = charger_view(&status);

    LSError lserror;
    LSErrorInit(&lserror);

    char *payload = ChargerDockStatusPayload(&status, &v, true);

    BATTERYDLOG(LOG_DEBUG,"%s: Sending payload : %s",__func__,payload);
    bool retVal = LSMessageReply(sh, message, payload, &lserror);
    if (!retVal)
    {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }
    g_free(payload);

    /*
     * The client now knows something the bus may not: if this read differs
     * from what was last broadcast, nyx's callback did not come (or has not
     * come yet). Say it to everyone, not just the one who asked.
     */
    ChargerBroadcast(&status, false);

    return TRUE;
}

/*
 * Broadcast status if it differs from what was last broadcast, or always
 * when force. Returns true when something was sent.
 */
static bool
ChargerBroadcast(const nyx_charger_status_t *status, bool force)
{
    charger_view_t v = charger_view(status);
    bool changed = !lastBroadcastValid || charger_view_differs(&lastBroadcast, &v);
    bool ok = true;

    g_debug("%s: wired=%d dock=%d any=%d charging=%d (last: valid=%d wired=%d dock=%d any=%d charging=%d)%s",
            __func__, v.wired, v.dock, v.any, v.charging,
            lastBroadcastValid, lastBroadcast.wired, lastBroadcast.dock,
            lastBroadcast.any, lastBroadcast.charging, force ? " forced" : "");

    if (!force && !changed)
        return false;

    LSError lserror;
    LSErrorInit(&lserror);

    char *payload = ChargerDockStatusPayload(status, &v, false);

    BATTERYDLOG(LOG_DEBUG,"%s: Sending payload : %s",__func__,payload);

    if (!LSSignalSend(GetLunaServiceHandle(),
            "luna://com.webos.service.battery/com/palm/power/USBDockStatus",
            payload, &lserror))
    {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
        ok = false;
    }
    g_free(payload);

    payload = g_strdup_printf("{\"type\":\"%s\",\"name\":\"%s\",\"connected\":%s,\"current_mA\":%d,\"message_source\":\"batteryd\"}",
            charger_view_type(&v),
            ChargerNameToString(status->connected),
            v.any ? "true" : "false",
            status->charger_max_current);
    BATTERYDLOG(LOG_DEBUG,"%s: Sending payload : %s",__func__,payload);

    if (!LSSignalSend(GetLunaServiceHandle(),
            "luna://com.webos.service.battery/com/palm/power/chargerStatus",
            payload, &lserror))
    {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
        ok = false;
    }
    g_free(payload);

    if (force || !lastBroadcastValid || lastBroadcast.any != v.any)
    {
        payload = g_strdup_printf("{\"connected\":%s}", v.any ? "true" : "false");

        BATTERYDLOG(LOG_DEBUG,"%s: Sending payload : %s",__func__,payload);

        if (!LSSignalSend(GetLunaServiceHandle(),
                "luna://com.webos.service.battery/com/palm/power/chargerConnected",
                payload, &lserror))
        {
            LSErrorPrint(&lserror, stderr);
            LSErrorFree(&lserror);
            ok = false;
        }

        /* sleepd subscribes to chargerConnected in the root category ("/"),
         * not /com/palm/power, so also emit it there; otherwise sleepd never
         * sees charger plug/unplug and suspend-thrashes while charging. */
        if (!LSSignalSend(GetLunaServiceHandle(),
                "luna://com.webos.service.battery/chargerConnected",
                payload, &lserror))
        {
            LSErrorPrint(&lserror, stderr);
            LSErrorFree(&lserror);
            ok = false;
        }
        g_free(payload);
    }

    /* A send that failed is retried by the next resync: leave the record. */
    if (ok)
    {
        lastBroadcast = v;
        lastBroadcastValid = true;
    }

    // Iterate through both charging as well as battery state machines. Is this required ??
//    ChargingLogicUpdate(NYX_NO_NEW_EVENT);

    return true;
}

void sendChargerStatus(bool bOnlyIfChanged)
{
    nyx_charger_status_t status;

    if (!ChargerRead(&status, bOnlyIfChanged ? "notify" : "signal"))
        return;

    ChargerBroadcast(&status, !bOnlyIfChanged);
}

/*
 * Look at the charger now, without being told to by nyx, and broadcast if it
 * differs from what the bus was last told.
 *
 * nyx's status callback is driven by power_supply uevents, and on some
 * kernels the supply nyx reads is not one that emits them (sargo's pc_port),
 * so an edge can go unannounced. This runs every CHARGER_RESYNC_SECONDS and
 * whenever nyx reports a battery change, which a charger change tends to
 * cause within a few readings.
 */
void ChargerResync(const char *why)
{
    nyx_charger_status_t status;

    if (!ChargerRead(&status, why))
        return;

    if (ChargerBroadcast(&status, false))
    {
        BATTERYDLOG(LOG_INFO, "charger state changed without a nyx notification (%s)", why);
    }
}

static gboolean
ChargerResyncTimer(gpointer data)
{
    ChargerResync("periodic");
    return G_SOURCE_CONTINUE;
}

void notifyChargerStatus(nyx_device_handle_t handle, nyx_callback_status_t status, void* data)
{
    sendChargerStatus(true);
}

void notifyStateChange(nyx_device_handle_t handle, nyx_callback_status_t status, void* data)
{
    nyx_charger_event_t new_event;
    nyx_charger_status_t status_now;

    nyx_error_t err = nyx_charger_query_charger_event(nyxDev,&new_event);
    if(err != NYX_ERROR_NONE)
    {
        BATTERYDLOG(LOG_ERR,"%s: nyx_charger_query_charger_event returned with error : %d",__func__,err);
        /* new_event is untouched stack, and it drives the state machine. */
        return;
    }

    /*
     * Read the charger before acting on the edge. The state machine asks
     * ChargerIsConnected() what is plugged in, and that answers from currStatus,
     * which only a read updates - so without this the machine can be driven by a
     * CONNECTED edge while still being told nothing is connected, and the two
     * charge states then hand back to each other. ChargeStateIterate() no longer
     * spins when they do, but it would still mean charging decisions taken
     * against a charger view older than the event that prompted them.
     *
     * A read that fails leaves currStatus alone and says so; carry on either
     * way, because the event still has to be handled.
     */
    (void) ChargerRead(&status_now, "state change");

    handle_charger_event(new_event);
}

bool
chargerStatusQuerySignal(LSHandle *sh,
                   LSMessage *message, void *user_data)
{
    sendChargerStatus(false);
    return true;
}

bool
chargerEnableCharging(int *max_charging_current)
{
    nyx_charger_status_t status;
    nyx_error_t err = nyx_charger_enable_charging(nyxDev,&status);
    if(err != NYX_ERROR_NONE)
    {
        BATTERYDLOG(LOG_ERR,"%s: nyx_charger_enable_charging returned with error : %d",__func__,err);
        return false;
    }

    /* nyx just filled in status; currStatus is whatever the last read saw. */
    if (max_charging_current)
        *max_charging_current = status.charger_max_current;
    battery_set_wakeup_percentage(true,false);
    return true;
}

bool
chargerDisableCharging(void)
{
    nyx_charger_status_t status;
    nyx_error_t err = nyx_charger_disable_charging(nyxDev,&status);
    if(err != NYX_ERROR_NONE)
    {
        BATTERYDLOG(LOG_ERR,"%s: nyx_charger_disable_charging returned with error : %d",__func__,err);
    }
    battery_set_wakeup_percentage(false,false);

    return true;
}

void getNewEvent(void)
{
    nyx_charger_event_t new_event;
    nyx_error_t err = nyx_charger_query_charger_event(nyxDev,&new_event);
    if(err != NYX_ERROR_NONE)
    {
        BATTERYDLOG(LOG_ERR,"%s: nyx_charger_query_charger_event returned with error : %d",__func__,err);
        /* new_event is untouched stack, and it drives the state machine. */
        return;
    }

    handle_charger_event(new_event);
}

/**
 * @brief Initialize tha NYX api for charging and send the batteryd config parameters.
 */

int ChargerInit(void)
{
    int ret = 0;
    nyx_init();

    nyx_error_t error = NYX_ERROR_NONE;
    nyx_device_iterator_handle_t iterator = NULL;

    error = nyx_device_get_iterator(NYX_DEVICE_CHARGER, NYX_FILTER_DEFAULT, &iterator);
    if(error != NYX_ERROR_NONE || iterator == NULL) {
       goto error;
    }
    else if (error == NYX_ERROR_NONE)
    {
        nyx_device_id_t id = NULL;
        while ((error = nyx_device_iterator_get_next_id(iterator,
            &id)) == NYX_ERROR_NONE && NULL != id)
        {
            g_debug("Batteryd: Charger device id \"%s\" found",id);
            error = nyx_device_open(NYX_DEVICE_CHARGER, id, &nyxDev);
            if(error != NYX_ERROR_NONE)
            {
                goto error;
            }
            break;
        }
    }

    memset(&currStatus,0,sizeof(nyx_charger_status_t));
    lastBroadcastValid = false;

    LSError lserror;
    LSErrorInit(&lserror);
    bool retVal;

    retVal = LSCall(GetLunaServiceHandle(),
        "luna://com.palm.lunabus/signal/addmatch",
            "{\"category\":\"/com/palm/power\","
             "\"method\":\"chargerStatusQuery\"}",
             chargerStatusQuerySignal, NULL, NULL, &lserror);
    if (!retVal)
        goto lserror;

    nyx_charger_register_charger_status_callback(nyxDev,notifyChargerStatus,NULL);

    if (!gChargeConfig.skip_battery_check && !gChargeConfig.disable_charging)
        nyx_charger_register_state_change_callback(nyxDev,notifyStateChange,NULL);

    g_timeout_add_seconds(CHARGER_RESYNC_SECONDS, ChargerResyncTimer, NULL);

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
    g_critical("Batteryd: No charger device found\n");
    gChargeConfig.skip_battery_check = 1;
    if(iterator)
        nyx_device_release_iterator(iterator);
//    abort();
    return 0;
}

INIT_FUNC(INIT_FUNC_MIDDLE, ChargerInit);
