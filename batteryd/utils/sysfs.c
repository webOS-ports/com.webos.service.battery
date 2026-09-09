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
 * @file sysfs.c
 *
 * @brief Get or set sysfs entries.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include "logging.h"
#include "batteryd_debug.h"

#define LOG_DOMAIN "sysfs:"

/**
 * Returns string in pre-allocated buffer.
 */
int
SysfsGetString(const char *path, char *ret_string, size_t maxlen)
{
    GError *gerror = NULL;
    char *contents = NULL;

    if (!path || !ret_string || maxlen == 0 ||
        !g_file_get_contents(path, &contents, NULL, &gerror)) {
        if (gerror) {
            BATTERYDLOG(LOG_CRIT, "%s: %s", __FUNCTION__, gerror->message);
            g_error_free(gerror);
        }
        return -1;
    }

    g_strstrip(contents);
    g_strlcpy(ret_string, contents, maxlen);

    g_free(contents);

    return 0;
}

/**
 * @returns 0 on success or -1 on error
 */
int
SysfsWriteString(const char *path, const char *string)
{
    int fd;
    ssize_t n;
    
    fd = open(path, O_WRONLY);
    if (fd < 0)
        return -1;

    n = write(fd, string, strlen(string));
    close(fd);
    return n >= 0 ? 0 : -1;
}
