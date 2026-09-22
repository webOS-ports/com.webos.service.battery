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
 * charger_eval.h is what every charger signal and reply is derived from.
 * These pin down that the derived views agree with each other for each
 * shape nyx's charger module produces, and that the change test that gates
 * a broadcast sees the edges it must. Host-runnable: tests/run-host-tests.sh
 */

#include <glib.h>
#include <string.h>

#include "../batteryd/charging/charger_eval.h"

static nyx_charger_status_t make(int connected, int powered, bool charging)
{
    nyx_charger_status_t s;

    memset(&s, 0, sizeof(s));
    s.connected = connected;
    s.powered = powered;
    s.is_charging = charging;
    return s;
}

/* nothing at all */
static void test_none(void)
{
    nyx_charger_status_t s = make(0, 0, false);
    charger_view_t v = charger_view(&s);

    g_assert_false(v.wired);
    g_assert_false(v.dock);
    g_assert_false(v.any);
    g_assert_false(v.charging);
    g_assert_cmpstr(charger_view_type(&v), ==, "none");
}

/* the USB slot: a PC, as nyx reports pc_port on sargo */
static void test_usb_slot(void)
{
    nyx_charger_status_t s = make(NYX_CHARGER_PC_CONNECTED, NYX_CHARGER_USB_POWERED, true);
    charger_view_t v = charger_view(&s);

    g_assert_true(v.wired);
    g_assert_false(v.dock);
    g_assert_true(v.any);
    g_assert_cmpstr(charger_view_type(&v), ==, "usb");
}

/*
 * The AC slot: how nyx reports sargo's "usb" node on a wall charger. Used to
 * be USBConnected:false / connected:true, i.e. the display manager saw no
 * charger while sleepd saw one.
 */
static void test_ac_slot_is_wired(void)
{
    nyx_charger_status_t s = make(NYX_CHARGER_WALL_CONNECTED, NYX_CHARGER_DIRECT_POWERED, true);
    charger_view_t v = charger_view(&s);

    g_assert_true(v.wired);
    g_assert_false(v.dock);
    g_assert_true(v.any);
    g_assert_cmpstr(charger_view_type(&v), ==, "usb");
}

/* an inductive dock, powered */
static void test_dock(void)
{
    nyx_charger_status_t s = make(NYX_CHARGER_INDUCTIVE_CONNECTED, NYX_CHARGER_INDUCTIVE_POWERED, true);
    charger_view_t v = charger_view(&s);

    g_assert_false(v.wired);
    g_assert_true(v.dock);
    g_assert_true(v.any);
    g_assert_cmpstr(charger_view_type(&v), ==, "inductive");
}

/* only a Touch/Wireless node online: nyx sets no flag but says charging */
static void test_unclassified_supply_counts(void)
{
    nyx_charger_status_t s = make(0, 0, true);
    charger_view_t v = charger_view(&s);

    g_assert_false(v.wired);
    g_assert_false(v.dock);
    g_assert_true(v.any);
    g_assert_true(v.charging);
}

/* the gate: an unplug is a difference, a repeat is not */
static void test_differs(void)
{
    nyx_charger_status_t on = make(NYX_CHARGER_PC_CONNECTED, NYX_CHARGER_USB_POWERED, true);
    nyx_charger_status_t off = make(0, 0, false);
    nyx_charger_status_t wall = make(NYX_CHARGER_WALL_CONNECTED, NYX_CHARGER_DIRECT_POWERED, true);
    charger_view_t von = charger_view(&on);
    charger_view_t voff = charger_view(&off);
    charger_view_t vwall = charger_view(&wall);

    g_assert_true(charger_view_differs(&von, &voff));
    g_assert_true(charger_view_differs(&voff, &von));
    g_assert_false(charger_view_differs(&von, &von));
    g_assert_false(charger_view_differs(&voff, &voff));

    /* pc_port dropping while usb still reads online is not an unplug */
    g_assert_false(charger_view_differs(&von, &vwall));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/charger_eval/none", test_none);
    g_test_add_func("/charger_eval/usb-slot", test_usb_slot);
    g_test_add_func("/charger_eval/ac-slot-is-wired", test_ac_slot_is_wired);
    g_test_add_func("/charger_eval/dock", test_dock);
    g_test_add_func("/charger_eval/unclassified-supply-counts", test_unclassified_supply_counts);
    g_test_add_func("/charger_eval/differs", test_differs);
    return g_test_run();
}
