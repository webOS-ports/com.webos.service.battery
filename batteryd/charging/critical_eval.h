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
 * @file critical_eval.h
 *
 * @brief Whether a low state-of-charge reading should count towards a
 * critical-battery shutdown.
 *
 * Kept free of batteryd state so the decision can be tested on the host.
 */

#ifndef _CRITICAL_EVAL_H_
#define _CRITICAL_EVAL_H_

#include <stdbool.h>

/*
 * A cell that is really at a few percent sits well below this, even at rest.
 * Above it a low percentage is the gauge being wrong, not the battery being
 * flat: MediaTek's fuel gauge restores its UI percentage from the PMIC RTC
 * and can then report 1% at 4.0 V for hours (seen on the Mindphone, MT6739).
 */
#define CRITICAL_PERCENT_PLAUSIBLE_MAX_MV 3700

typedef enum
{
    CRITICAL_PERCENT_NO,        /**< above the critical level, or check off */
    CRITICAL_PERCENT_YES,       /**< at or below it, and believable */
    CRITICAL_PERCENT_IMPLAUSIBLE, /**< at or below it, but the voltage says otherwise */
} critical_percent_t;

/**
 * @param percentage        reported state of charge
 * @param voltage_mv        battery voltage in mV, <= 0 when unknown
 * @param critical_percent  shut down at or below this, <= 0 disables it
 */
static inline critical_percent_t
critical_percent_eval(int percentage, int voltage_mv, int critical_percent)
{
    if (critical_percent <= 0 || percentage > critical_percent)
        return CRITICAL_PERCENT_NO;

    /* an unknown voltage (<= 0) falls through: trust the percentage */
    if (voltage_mv >= CRITICAL_PERCENT_PLAUSIBLE_MAX_MV)
        return CRITICAL_PERCENT_IMPLAUSIBLE;

    return CRITICAL_PERCENT_YES;
}

#endif /* _CRITICAL_EVAL_H_ */
