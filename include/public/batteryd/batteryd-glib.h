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


#ifndef _LIBBATTERYD_GLIB_H_
#define _LIBBATTERYD_GLIB_H_

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

void BatterydGmainAttach(GMainLoop *mainLoop);

#ifdef __cplusplus
} // extern "C"
#endif

#endif //  _LIBBATTERYD_GLIB_H_
