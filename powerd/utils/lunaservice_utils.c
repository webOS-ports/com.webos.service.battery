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


#include "lunaservice_utils.h"

void
LSMessageReplyErrorUnknown(LSHandle *sh, LSMessage *message)
{
    LSError lserror;
    LSErrorInit(&lserror);

    bool retVal = LSMessageReply(sh, message, "{\"returnValue\":false,"
        "\"errorText\":\"Unknown Error.\"}", &lserror);
    if (!retVal)
    {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }
}

void
LSMessageReplyErrorInvalidParams(LSHandle *sh, LSMessage *message)
{
    LSError lserror;
    LSErrorInit(&lserror);

    bool retVal = LSMessageReply(sh, message, "{\"returnValue\":false,"
        "\"errorText\":\"Invalid parameters.\"}", NULL);
    if (!retVal)
    {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }
}

void
LSMessageReplyErrorBadJSON(LSHandle *sh, LSMessage *message)
{
    LSError lserror;
    LSErrorInit(&lserror);

    bool retVal = LSMessageReply(sh, message, "{\"returnValue\":false,"
        "\"errorText\":\"Malformed json.\"}", NULL);
    if (!retVal)
    {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }
}

void
LSMessageReplySuccess(LSHandle *sh, LSMessage *message)
{
    LSError lserror;
    LSErrorInit(&lserror);

    bool retVal = LSMessageReply(sh, message, "{\"returnValue\":true}",
        NULL);
    if (!retVal)
    {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }
}

