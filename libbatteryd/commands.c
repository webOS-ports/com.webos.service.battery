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
 * @file commands.c
 *
 * @brief This file contains APIs for registering new clients with batteryd.
 */

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <glib.h>
#include <json.h>
#include <luna-service2/lunaservice.h>

#include "batteryd.h"
#include "init.h"

#define BATTERYD_IPC_NAME "com.webos.service.battery"
#define BATTERYD_DEFAULT_CATEGORY "/com/palm/power/"

typedef struct CallbackHelper
{
    void           *callback;
    LSMessageToken  token;
} CBH;

#define CBH_INIT(helper) \
CBH helper = {0, 0}

GMainLoop *gMainLoop = NULL;
bool gOwnMainLoop = false;
bool gOwnLunaService = false;

static LSHandle *gServiceHandle = NULL;

/** 
* @brief Use your own LSHandle.  This MUST be called before BatterydCLientInit()
* 
* @param  mainLoop 
*/
void
_LSHandleAttach(LSHandle *sh)
{
    if (sh)
    {
        gServiceHandle = sh;
        gOwnLunaService = true;
    }
    else
    {
        gServiceHandle = NULL;
        gOwnLunaService = false;
    }
}

static void
SignalCancel(CBH *helper)
{
    bool retVal;
    LSError lserror;
    LSErrorInit(&lserror);

    retVal = LSCallCancel(gServiceHandle, helper->token, &lserror);
    if (!retVal)
    {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    helper->token = 0;
}

static void
SignalRegister(const char *signalName, LSFilterFunc callback, CBH *helper)
{
    bool retVal;
    LSError lserror;
    LSErrorInit(&lserror);

    char *payload = g_strdup_printf(
            "{\"category\":\"/com/palm/power\",\"method\":\"%s\"}",
            signalName);

    retVal = LSCall(gServiceHandle,
        "luna://com.palm.lunabus/signal/addmatch", payload, 
        callback, helper, &helper->token, &lserror);
    if (!retVal)
    {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    g_free(payload);
}

static void
SendMessage(LSFilterFunc callback, const char *uri, const char *payload, ...)
{
    bool retVal;
    char *buffer;

    LSError lserror;
    LSErrorInit(&lserror);
    
    va_list args;
    va_start(args, payload);
    buffer  = g_strdup_vprintf(payload, args);
    va_end(args);

    retVal = LSCall(gServiceHandle, uri, buffer,
                    callback, NULL, NULL, &lserror);
    if (!retVal)
    {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    g_free(buffer);
}

static void
SendSignal(LSFilterFunc callback, const char *uri, const char *payload, ...)
{
    bool retVal;
    char *buffer;

    LSError lserror;
    LSErrorInit(&lserror);

    va_list args;
    va_start(args, payload);
    buffer  = g_strdup_vprintf(payload, args);
    va_end(args);

    retVal = LSSignalSend(gServiceHandle, uri, buffer, &lserror);
    if (!retVal)
    {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    g_free(buffer);
}

/** 
* @brief Request a battery status notification.
*/
void
BatterydGetBatteryStatusNotification(void)
{
    SendSignal(NULL, "luna://" BATTERYD_IPC_NAME BATTERYD_DEFAULT_CATEGORY
                "batteryStatusQuery", "{}");
}

/** 
* @brief Helper that translates from jsonized message into form for use
*        with callback.
* 
* @param  sh 
* @param  message 
* @param  ctx 
* 
* @retval
*/
static bool
_ClientBatteryStatusCallback(LSHandle *sh, LSMessage *message, void *ctx)
{
    CBH *helper = (CBH *)ctx;
    if (!helper || !helper->callback) return true;

    const char *payload = LSMessageGetPayload(message);
    struct json_object *object = json_tokener_parse(payload);
    if (!object) {
     	goto end;
    }

    int percent;
    int temp_C;
    int current_mA;
    int voltage_mV;

    percent = json_object_get_int(
            json_object_object_get(object, "percent"));

    temp_C = json_object_get_int(
            json_object_object_get(object, "temperature_C"));

    current_mA = json_object_get_int(
            json_object_object_get(object, "current_mA"));

    voltage_mV = json_object_get_int(
            json_object_object_get(object, "voltage_mV"));

    ((BatterydCallback_Int32_4)helper->callback)
                (percent, temp_C, current_mA, voltage_mV);

end:
	if (object) json_object_put(object);
    return true;
}

/** 
* @brief Register a callback for battery status notifications.
* 
* @param  callback_function 
* Callback is called with the following parameters:
* callback(percent, temperature, current, voltage);
*/
void
BatterydBatteryStatusRegister(BatterydCallback_Int32_4 callback_function)
{
    static CBH_INIT(helper);

    if (helper.token)
    {
        SignalCancel(&helper);
    }

    helper.callback = (void*)callback_function;

    SignalRegister("batteryStatus", _ClientBatteryStatusCallback, &helper);
}

/** 
* @brief Unimplemented.
* 
* @param  callback_function 
*/
void
BatterydGetChargerStatusNotification(void)
{
    g_warning("%s: This function is not implemented", __FUNCTION__);
}

/** 
* @brief Unimplemented.
* @param callback - Function to be called when charger state changes.
* 
* @param  callback_function 
* Callback is called with the following parameters:
* callback(source, current_mA).
*/
void
BatterydChargerStatusRegister(BatterydCallback_String_Int32 callback_function)
{
    g_warning("%s: This function is not implemented", __FUNCTION__);
}

static void *
_BatterydIPCThread(void *ctx)
{
    g_main_loop_run(gMainLoop);
    return NULL;
}

static bool
_identify_callback(LSHandle *sh, LSMessage *msg, void *ctx)
{
    struct json_object * object =
                json_tokener_parse(LSMessageGetPayload(msg));
    if (!object) goto error;

    bool subscribed = json_object_get_boolean(
            json_object_object_get(object, "subscribed"));
    const char *clientId = json_object_get_string(
            json_object_object_get(object, "clientId"));

    if (!subscribed || !clientId)
    {
        g_critical("%s: Could not subscribe to batteryd %s.", __FUNCTION__,
                   LSMessageGetPayload(msg));
        goto end;
    }

    BatterydHandle *handle = BatterydGetHandle(); 
    BatterydSetClientId(handle, clientId);

    char *message = g_strdup_printf(
            "{\"register\":true,\"clientId\":\"%s\"}", clientId);

    g_free(message);

error:
end:
    if (object) json_object_put(object);
    return true;
}

/** 
* @brief Called to re-register with batteryd if batteryd crashes.
* 
* @param  sh 
* @param  msg 
* @param  ctx 
* 
* @retval
*/
static bool
_batteryd_server_up(LSHandle *sh, LSMessage *msg, void *ctx)
{
    bool connected;

    struct json_object *object = json_tokener_parse(LSMessageGetPayload(msg));
    if (!object) goto error;

    connected = json_object_get_boolean(
                json_object_object_get(object, "connected"));

    if (connected)
    {
        g_debug("%s connected was true (batteryd is already running)", __FUNCTION__);
        BatterydHandle *handle = BatterydGetHandle();

        /* Send our name to batteryd. */
        SendMessage(_identify_callback,
                "luna://com.webos.service.battery/com/palm/power/identify",
                "{\"subscribed\":true,\"clientName\":\"%s\"}",
                handle->clientName);
    }

end:
    if (object) json_object_put(object);
    return true;

error:
    g_critical("%s: Error registering with com.webos.service.battery", __FUNCTION__);
    goto end;
}

void
_BatterydClientIPCRun(void)
{
    bool retVal;
    LSError lserror;
    LSErrorInit(&lserror);

    if (!gServiceHandle)
    {
        retVal = LSRegister(NULL, &gServiceHandle, &lserror);
        if (!retVal) goto error;

        if (!gMainLoop)
        {
            int ret;

            GMainContext *context = g_main_context_new();
            if (!context) goto error;

            gMainLoop = g_main_loop_new(context, FALSE);
            g_main_context_unref(context);
            if (!gMainLoop) goto error;

            pthread_attr_t attr;
            pthread_attr_init(&attr);

            ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            assert(ret == 0);

            pthread_t tid;
            pthread_create(&tid, &attr, _BatterydIPCThread, NULL);
        }

        retVal = LSGmainAttach(gServiceHandle, gMainLoop, &lserror);
        if (!retVal) goto error;
    }

    retVal = LSCall(gServiceHandle,
        "luna://com.palm.lunabus/signal/registerServerStatus",
        "{\"serviceName\":\"com.webos.service.battery\"}", _batteryd_server_up,
        NULL, NULL, &lserror);
    if (!retVal) goto error;

    return;
error:
    LSErrorPrint(&lserror, stderr);
    LSErrorFree(&lserror);
    return;
}


/** 
* @brief Cleanup all IPC.
*/
void
_BatterydClientIPCStop(void)
{
    if (gMainLoop)
    {
        if (!gOwnMainLoop)
            g_main_loop_quit(gMainLoop);

        g_main_loop_unref(gMainLoop);

        gMainLoop = NULL;
        gOwnMainLoop = false;
    }

    if (gServiceHandle && !gOwnLunaService)
    {
        bool retVal;
        LSError lserror;
        LSErrorInit(&lserror);

        retVal = LSUnregister(gServiceHandle, &lserror);
        if (!retVal)
        {
            LSErrorPrint(&lserror, stderr);
            LSErrorFree(&lserror);
        }
    }
    gServiceHandle = NULL;
    gOwnLunaService = false;
}
