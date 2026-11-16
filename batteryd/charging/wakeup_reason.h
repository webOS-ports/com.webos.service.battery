/* @@@LICENSE
*
*      Copyright (c) 2026 LuneOS
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
 * @file wakeup_reason.h
 *
 * @brief Reading what the kernel says woke it from suspend.
 *
 * Kept free of batteryd state so it can be tested on the host.
 */

#ifndef _WAKEUP_REASON_H_
#define _WAKEUP_REASON_H_

#include <glib.h>

/**
 * Where a kernel says what woke it, in the order they are tried. A kernel has
 * at most one of them, and many have none:
 *
 * - /sys/power/wakeup_event_list: Palm's webOS kernels only.
 * - /sys/kernel/wakeup_reasons/last_resume_reason: Android common kernels.
 *   Empty when the platform's interrupt code does not report to it, as on the
 *   Titan's MT6771 kernel with MediaTek's legacy EINT and PMIC drivers.
 * - /sys/power/pm_wakeup_irq: mainline kernels with CONFIG_PM_SLEEP_DEBUG.
 *   Reading it fails with ENODATA when no interrupt woke the system.
 */
#define WAKEUP_REASON_FILES                             \
    {                                                   \
        "/sys/power/wakeup_event_list",                 \
        "/sys/kernel/wakeup_reasons/last_resume_reason", \
        "/sys/power/pm_wakeup_irq",                     \
    }

/**
 * Copies the first non-empty wakeup reason among @p files into @p buf,
 * stripped of surrounding whitespace and truncated to fit.
 *
 * A file that is missing, unreadable or empty is skipped without a message:
 * on most kernels that is the normal case.
 *
 * @param files  paths to try, in order
 * @param nfiles number of entries in @p files
 * @param buf    receives the reason
 * @param len    size of @p buf, at least 1
 *
 * @returns the index in @p files the reason came from, or -1 if none had one
 *          (@p buf is then the empty string)
 */
static inline int
wakeup_reason_read(const char *const *files, size_t nfiles, char *buf, size_t len)
{
    size_t i;

    buf[0] = '\0';

    for (i = 0; i < nfiles; i++)
    {
        gchar *contents = NULL;

        if (!g_file_get_contents(files[i], &contents, NULL, NULL))
            continue;

        g_strstrip(contents);
        if (contents[0] != '\0')
        {
            g_strlcpy(buf, contents, len);
            g_free(contents);
            return (int) i;
        }
        g_free(contents);
    }

    return -1;
}

#endif /* _WAKEUP_REASON_H_ */
