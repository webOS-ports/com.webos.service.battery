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
 * @file charger_eval.h
 *
 * One evaluation of a nyx charger status that every signal and reply is
 * derived from, so that they cannot disagree with each other.
 *
 * USBDockStatus.USBConnected used to be (powered & USB_POWERED), while
 * chargerStatus.connected and chargerConnected.connected were (connected
 * != 0). nyx fills "powered" with USB_POWERED only for the supply in its USB
 * slot and with DIRECT_POWERED for the one in its AC slot; on a Qualcomm
 * smb2/smb5 part those two slots are the two halves of the same USB input
 * (pc_port for an SDP source, usb for everything else), so a wall charger
 * reported USBConnected:false to the display manager and connected:true to
 * sleepd at the same time, and an unplug seen by one half and not the other
 * produced a disconnect on one signal and nothing on the other.
 *
 * Header-only and free of luna-service so it can be unit-tested on a host.
 */

#ifndef _CHARGER_EVAL_H_
#define _CHARGER_EVAL_H_

#include <stdbool.h>
#include <nyx/common/nyx_charger_common.h>

typedef struct
{
    bool wired;     /**< a charger on a cable: USB, wall-via-USB, or mains */
    bool dock;      /**< an inductive dock */
    bool any;       /**< anything at all that counts as a charger */
    bool charging;  /**< nyx says the battery is being charged */
} charger_view_t;

static inline charger_view_t
charger_view(const nyx_charger_status_t *s)
{
    charger_view_t v;

    v.wired = (s->connected & (NYX_CHARGER_PC_CONNECTED |
                               NYX_CHARGER_WALL_CONNECTED |
                               NYX_CHARGER_DIRECT_CONNECTED)) != 0 ||
              (s->powered & (NYX_CHARGER_USB_POWERED |
                             NYX_CHARGER_DIRECT_POWERED)) != 0;

    v.dock = (s->connected & NYX_CHARGER_INDUCTIVE_CONNECTED) != 0 ||
             (s->powered & NYX_CHARGER_INDUCTIVE_POWERED) != 0;

    /*
     * A supply nyx could not classify (only "online" on a Touch or Wireless
     * node) sets neither flag but does set is_charging; it is still a
     * charger as far as sleepd's veto is concerned.
     */
    v.any = v.wired || v.dock || s->connected != 0 || s->powered != 0 ||
            s->is_charging;

    v.charging = s->is_charging;

    return v;
}

static inline bool
charger_view_differs(const charger_view_t *a, const charger_view_t *b)
{
    return a->wired != b->wired || a->dock != b->dock ||
           a->any != b->any || a->charging != b->charging;
}

/** The "type" of chargerStatus: what the display manager keys off. */
static inline const char *
charger_view_type(const charger_view_t *v)
{
    if (v->dock)
        return "inductive";
    if (v->wired)
        return "usb";
    return "none";
}

#endif /* _CHARGER_EVAL_H_ */
