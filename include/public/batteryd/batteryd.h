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


#ifndef _LIBBATTERYD_H_
#define _LIBBATTERYD_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Interface to libbatteryd.
 * See doxygen pages for more information.
 */

typedef void (*BatterydCallback)(void);
typedef void (*BatterydCallback_Int32)(int);
typedef void (*BatterydCallback_Int32_4)(int, int, int, int);
typedef void (*BatterydCallback_String_Int32)(const char *, int);

void BatterydClientInit(const char *clientName);
void BatterydClientDeinit(void);

void BatterydGetChargerStatusNotification(void);

void BatterydChargerStatusRegister(BatterydCallback_String_Int32 callback);

void BatterydGetBatteryStatusNotification(void);

void BatterydBatteryStatusRegister(BatterydCallback_Int32_4 callback);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _LIBBATTERYD_H_
