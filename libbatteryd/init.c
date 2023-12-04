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

#include <assert.h>

#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>

#include <luna-service2/lunaservice.h>

#include "init.h"
#include "batteryd_debug.h"

void _BatterydClientIPCRun(void);
void _BatterydClientIPCStop(void);

void _LSHandleAttach(LSHandle *sh);

extern GMainLoop *gMainLoop;
extern bool gOwnMainLoop;

static BatterydHandle sHandle =
{
    .clientName = "",
    .clientId = NULL, 
    .lock     = PTHREAD_MUTEX_INITIALIZER,
};

static void
BatterydHandleInit(BatterydHandle *handle)
{
    if (handle->clientId)
        g_free(handle->clientId);

    handle->clientName = "";
    handle->clientId = NULL; 
}

BatterydHandle*
BatterydGetHandle(void)
{
    return &sHandle;
}

/** 
* @brief Register as a power-aware client with name 'clientName'.
* 
* @param  clientName 
*/
void
BatterydClientInit(const char *clientName)
{
    BatterydHandleInit(&sHandle);

    sHandle.clientName = clientName;

    _BatterydClientIPCRun();
}

/** 
* @brief Register as a power-aware client using the existing
*        LunaService handle.
* 
* @param  clientName 
* @param  sh 
*/
void
BatterydClientInitLunaService(const char *clientName, LSHandle *sh)
{
    _LSHandleAttach(sh);
    BatterydClientInit(clientName);
}

/** 
* @brief Use this mainloop instead of creating a batteryd IPC
*        thread automatically.  This MUST be called before BatterydClientInit()
*        if you wish to use your own mainloop.
* 
* @param  mainLoop 
*/
void
BatterydGmainAttach(GMainLoop *mainLoop)
{
    if (mainLoop)
    {
        gMainLoop = g_main_loop_ref(mainLoop);
        gOwnMainLoop = true;
    }
    else
    {
        gMainLoop = NULL;
        gOwnMainLoop = false;
    }
}

void
BatterydClientLock(BatterydHandle *handle)
{
    int ret = pthread_mutex_lock(&sHandle.lock);
    assert(ret == 0);
}

void
BatterydClientUnlock(BatterydHandle *handle)
{
    int ret = pthread_mutex_unlock(&sHandle.lock);
    assert(ret == 0);
}

void
BatterydSetClientId(BatterydHandle *handle, const char *clientId)
{
    BatterydClientLock(handle);

    if (handle->clientId) g_free(handle->clientId);
    handle->clientId = g_strdup(clientId);

    BatterydClientUnlock(handle);
}

/** 
* @brief Stop being a batteryd client.
*        Implicit in this is we disconnect from any communications
*        from batteryd.
*/
void
BatterydClientDeinit(void)
{
    BatterydHandleInit(&sHandle);
    _BatterydClientIPCStop();

}
