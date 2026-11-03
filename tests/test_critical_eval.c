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

/*
 * critical_eval.h decides whether a low percentage counts towards the
 * critical-battery shutdown. Host-runnable: tests/run-host-tests.sh
 */

#include <glib.h>

#include "../batteryd/charging/critical_eval.h"

/* above the critical level: never critical, whatever the voltage */
static void test_above_level(void)
{
    g_assert_cmpint(critical_percent_eval(3, 3300, 2), ==, CRITICAL_PERCENT_NO);
    g_assert_cmpint(critical_percent_eval(50, 3900, 2), ==, CRITICAL_PERCENT_NO);
}

/* the check switched off with 0 or a negative level */
static void test_disabled(void)
{
    g_assert_cmpint(critical_percent_eval(0, 3300, 0), ==, CRITICAL_PERCENT_NO);
    g_assert_cmpint(critical_percent_eval(0, 3300, -1), ==, CRITICAL_PERCENT_NO);
}

/* a flat battery: low percentage and low voltage */
static void test_really_flat(void)
{
    g_assert_cmpint(critical_percent_eval(1, 3389, 2), ==, CRITICAL_PERCENT_YES);
    g_assert_cmpint(critical_percent_eval(2, 3500, 2), ==, CRITICAL_PERCENT_YES);
    g_assert_cmpint(critical_percent_eval(0, 3100, 2), ==, CRITICAL_PERCENT_YES);
}

/* the Mindphone's MediaTek gauge: 1% at 3.99 V */
static void test_gauge_wrong(void)
{
    g_assert_cmpint(critical_percent_eval(1, 3990, 2), ==, CRITICAL_PERCENT_IMPLAUSIBLE);
    g_assert_cmpint(critical_percent_eval(0, 4158, 2), ==, CRITICAL_PERCENT_IMPLAUSIBLE);
}

/* the limit itself: just below still counts, at the limit it does not */
static void test_limit(void)
{
    g_assert_cmpint(critical_percent_eval(1, CRITICAL_PERCENT_PLAUSIBLE_MAX_MV - 1, 2),
                    ==, CRITICAL_PERCENT_YES);
    g_assert_cmpint(critical_percent_eval(1, CRITICAL_PERCENT_PLAUSIBLE_MAX_MV, 2),
                    ==, CRITICAL_PERCENT_IMPLAUSIBLE);
}

/* nyx answers -1 when it cannot read the voltage: trust the percentage */
static void test_voltage_unknown(void)
{
    g_assert_cmpint(critical_percent_eval(1, -1, 2), ==, CRITICAL_PERCENT_YES);
    g_assert_cmpint(critical_percent_eval(1, 0, 2), ==, CRITICAL_PERCENT_YES);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/critical/above-level", test_above_level);
    g_test_add_func("/critical/disabled", test_disabled);
    g_test_add_func("/critical/really-flat", test_really_flat);
    g_test_add_func("/critical/gauge-wrong", test_gauge_wrong);
    g_test_add_func("/critical/limit", test_limit);
    g_test_add_func("/critical/voltage-unknown", test_voltage_unknown);
    return g_test_run();
}
