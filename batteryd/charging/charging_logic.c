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
* @file charging_logic.c
*
* @brief Charging logic for all devices. One or more of the state functions can be modified to have any
* device specific charging state machine.
*
*/

#include <string.h>
#include <stdlib.h>

#include "battery.h"
#include "batterypoll.h"
#include "init.h"
#include "batteryd_config.h"
#include "clock.h"
#include "logging.h"
#include "suspend.h"
#include "charging_logic.h"
#include "lunaservice_utils.h"
#include "main.h"
#include "sysfs.h"

#define LOG_DOMAIN "CHG_LOGIC: "

#define OVERCHARGE_RETRIES	3

#define BATTERY_MAX_TEMPERATURE_C	60



static const char * const debug_state_description[kChargeStateLast+1] =
{
    "idle",
    "charging",
    "fault",
    "chargecomplete",
    "shutdown",
    "shutdownwait",
    "last",
};


static ChargeState StateIdle(nyx_charger_event_t event);

static ChargeState StateCharging(nyx_charger_event_t event);

static ChargeState StateChargeComplete(nyx_charger_event_t event);
static ChargeState StateFault(nyx_charger_event_t event);

static ChargeState StateShutdown(nyx_charger_event_t event);
static ChargeState StateShutdownWait(nyx_charger_event_t event);


static struct ChargeStateNode kStateMachine[kChargeStateLast] = {
    { kChargeStateIdle,                StateIdle },
    { kChargeStateCharging,            StateCharging },
    { kChargeStateFault,               StateFault },
    { kChargeStateChargeComplete,      StateChargeComplete },
    { kChargeStateShutdown,            StateShutdown },
    { kChargeStateShutdownWait,        StateShutdownWait },
};

typedef enum
{
    CHARGING_ENABLED = 1,
    CHARGING_DISABLED = 0,
    CHARGING_NOTSET   = -1,
} ChargingEnable;

/**
 * @brief Current charging state.
 */
struct {
    ChargingEnable charging_enabled;

    int    max_charging_mA;

    ChargeState     current_state;
    struct ChargeStateNode state_node;

    const char *shutdown_reason;
} gCurrentChargeState;

/**
 * @addtogroup ChargingLogic
 * @{
 */
static void ChargeStateReset(void);

/**
 * @brief Turn Charging off by calling the device specific charging disable function.
 *
 * @param reason Reason for turning off charging.
 *
 * @retval
 */

void TurnChargingOff(const char *reason)
{
    if (gCurrentChargeState.charging_enabled != CHARGING_DISABLED)
    {
        BATTERYDLOG(LOG_INFO, "Turning charging off because of %s", reason);

        ChargeStateReset();
        chargerDisableCharging();

        gCurrentChargeState.charging_enabled = CHARGING_DISABLED;
    }
}


/**
 * @brief Turn Charging on if its not already on, by calling the device specific charging enable function.
 *
 * @retval
 */
bool
TurnChargingON(void)
{
    gCurrentChargeState.charging_enabled = CHARGING_ENABLED;
	return chargerEnableCharging(&gCurrentChargeState.max_charging_mA);
}

static void
_debug_battery_taper(nyx_battery_status_t * state, int taper_type, int min_current, int max_voltage, const char * taper_state)
{
  BATTERYDLOG(LOG_INFO, "debug_battery_taper (%s, type=%d, %fmAh, %d%%, %dC, %dmA, %dmV, (avg)%dmA, min=%dmA, max=%dmV, battery_state : %s)\n",
            debug_state_description[gCurrentChargeState.current_state],
            taper_type,
            state->capacity, state->percentage,
            state->temperature,
            state->current, state->voltage,
            state->avg_current,
            min_current,
            max_voltage,
            taper_state);
}

bool BatteryOverchargeFault(nyx_battery_status_t *state)
{
    static int overcharge_diag_state = 0;
    static int overcharge_count = 0;
    bool overcharge;
    double raw_mAh;
    double full_mAh;
    double age;
    double limit;

    raw_mAh = state->capacity_raw;
    full_mAh = state->capacity_full40;
    age = state->age;

    // Diagnostic to detect getting close to overcharge
    int new_diag_state = 0;
    limit = 0;

    if (raw_mAh >= (limit = (1.2 * (full_mAh * age / 100)))) {
      new_diag_state = 120;
    } else if (raw_mAh >= (limit = (1.1 * (full_mAh * age / 100)))) {
      new_diag_state = 110;
    } else if (raw_mAh >= (limit = (1.0 * (full_mAh * age / 100)))) {
      new_diag_state = 100;
    } else {
      new_diag_state = 0;
    }

    if (new_diag_state != overcharge_diag_state) {
        BATTERYDLOG(LOG_INFO, "charge capacity diag: "
                "raw = (%g) > %d%% of (full_mAh [%g] * age [%g] / 100) = (%g))",
                raw_mAh, new_diag_state, full_mAh, age, limit);
        overcharge_diag_state = new_diag_state;
        _debug_battery_taper(state, new_diag_state, 0, 0, "overcharge-debug");
    }

    limit = 1.2 * (full_mAh * age / 100);
    overcharge = (raw_mAh > limit);

    if (overcharge) {
        overcharge_count++;

        BATTERYDLOG(LOG_INFO, "%s seen %dx: "
                "raw = (%g) > 1.2 * (full_mAh [%g] * age [%g] / 100) = (%g)",
                __FUNCTION__, overcharge_count, raw_mAh, full_mAh, age, limit);
    }
    else {
        overcharge_count = 0;
    }

    return overcharge && (overcharge_count > OVERCHARGE_RETRIES);
}

/** State functions */

static void
ChargeStateReset(void)
{
    gCurrentChargeState.charging_enabled = CHARGING_NOTSET;
    gCurrentChargeState.shutdown_reason = "";
}

static int
ChargeStateInit(void)
{
    gCurrentChargeState.current_state = kChargeStateIdle;
    gCurrentChargeState.state_node = kStateMachine[kChargeStateIdle];

    ChargeStateReset();
    battery_get_ctia_params();

    return 0;
}

static void
ChargeStateTransitionLog(nyx_battery_status_t *state)
{
    static ChargeState last_state = kChargeStateLast;
    static int last_max_charging_mA = 0;

    if (last_state != gCurrentChargeState.current_state ||
        last_max_charging_mA != gCurrentChargeState.max_charging_mA)
    {
        last_state = gCurrentChargeState.current_state;
        last_max_charging_mA = gCurrentChargeState.max_charging_mA;

        BATTERYDLOG(LOG_INFO,
            "%s in %s (P: %d%%, T: %d C, C: %d mA, V: %d mV, AUTH %s)",
            __FUNCTION__,
            debug_state_description[gCurrentChargeState.current_state],
            state->percentage, state->temperature,
            state->current, state->voltage,
            BatteryIsAuthentic() ? "true": "false");
      }
}

/**
 * @brief Iterate through the charging state machine
 *
 */

static void
ChargeStateIterate(nyx_charger_event_t event)
{
    ChargeState next_state;
    nyx_battery_status_t state;
    unsigned int entered = 0;

    battery_read(&state);
    /*
        Drive the state machine until next_state goes to the pseudo-state kChargeStateLast.
        Subsequent calls to ChargeStateIterate() will call the current state function
        to potentially change state.
    */

    do {
        if (gCurrentChargeState.current_state < kChargeStateLast)
        {
            entered |= 1u << gCurrentChargeState.current_state;
        }

        next_state = gCurrentChargeState.state_node.function(event);

        ChargeStateTransitionLog(&state);

        /*
         * The event is an edge - a charger just plugged in, a charge just
         * completed - and it is consumed by the first handler that sees it. It
         * used to be handed unchanged to every handler this loop ran, and being
         * a by-value parameter nothing could clear it. That is half of how this
         * loop could fail to terminate:
         *
         *   StateIdle() sees NYX_CHARGER_CONNECTED, returns kChargeStateCharging.
         *   StateCharging() finds !ChargerIsConnected() - currStatus holds
         *   whatever the last read saw, and on a charger whose supply emits no
         *   uevent of its own that can still say nothing is connected - and
         *   returns kChargeStateIdle.
         *   StateIdle() sees the same CONNECTED bit again, and so on.
         *
         * Neither the event nor the state those two disagree about can change
         * while this loop holds the main loop. Measured on a Pixel 3a (sargo),
         * 2026-09-25: 516 seconds of CPU in the 519 since the charger was
         * plugged in, one core pinned, no charger or battery broadcast since the
         * edge arrived, and nothing in the journal to show for it because the
         * per-iteration log is formatted and then dropped. The resync timer that
         * exists to correct stale charger state cannot help - it is a timeout on
         * the main loop this loop is blocking.
         */
        event = NYX_NO_NEW_EVENT;

        if (kChargeStateLast != next_state)
        {
            gCurrentChargeState.current_state = next_state;
            gCurrentChargeState.state_node = kStateMachine[next_state];

            /*
             * Clearing the event is enough for the case above, since StateIdle()
             * with no event returns kChargeStateLast. This is the other half: two
             * states that hand back to each other on level state alone would
             * still spin. A pass that re-enters a state it has already run is
             * not making progress, so stop - the transition has been applied, so
             * the next event runs that handler again - and say so once.
             */
            if (next_state < kChargeStateLast && (entered & (1u << next_state)))
            {
                BATTERYDLOG(LOG_ERR,
                    "%s: %s re-entered in one pass, stopping to avoid spinning",
                    __FUNCTION__, debug_state_description[next_state]);
                break;
            }
        }
    } while (kChargeStateLast != next_state);
}

/**
* @brief Jump charging logic to the shutdown state.
*
* @param  reason
*/
static void
_JumpToShutdownState(const char *reason)
{
    if (gCurrentChargeState.current_state != kChargeStateShutdown &&
        gCurrentChargeState.current_state != kChargeStateShutdownWait) {

        ChargeState next_state = kChargeStateShutdown;

        gCurrentChargeState.shutdown_reason = reason;
        gCurrentChargeState.current_state = next_state;
        gCurrentChargeState.state_node = kStateMachine[next_state];
    }
}


void MachineShutdown(const char *reason)
{
	char *payload = g_strdup_printf("{\"reason\":\"%s\"}",reason);

	LSError lserror;
	LSErrorInit(&lserror);

	bool retVal = LSCallOneReply(GetLunaServiceHandle(),
			"luna://com.webos.service.sleep/shutdown/machineOff",
			payload, NULL, NULL, NULL, &lserror);
	g_free(payload);

	if (!retVal)
	{
		LSErrorPrint(&lserror, stderr);
		LSErrorFree(&lserror);
	}
}


/* consecutive readings at or below critical_percent before we act on it */
#define CRITICAL_PERCENT_SAMPLES 3

/**
 * @brief Decide whether the battery can still support running.
 *
 * Two of the three critical triggers live elsewhere: voltage arrives as
 * NYX_BATTERY_CRITICAL_VOLTAGE and temperature is checked against the CTIA
 * parameters, both in handle_charger_event(). This one covers the battery being
 * gone, and the state of charge.
 *
 * The percentage floor exists because the voltage trigger is the nyx module's
 * to raise, and a module that does not raise it leaves nothing at all between a
 * running device and a flat battery. It only applies while nothing is charging,
 * and it wants CRITICAL_PERCENT_SAMPLES readings in a row: gauges lie on the
 * way up from a cold boot - the rk818 on a PinePhone Pro ramps its reported
 * charge one point per six seconds from a stale saved value - and one bad
 * sample should not power the device off.
 *
 * @param state   latest readings, only read when state_valid
 * @param state_valid  false when battery_read() could not answer
 */
static bool
CheckCriticalLevels(const nyx_battery_status_t *state, bool state_valid)
{
    static int critical_samples = 0;

    /* Skip checks for people with fake batteries or bare-boards */
    if (gChargeConfig.skip_battery_check) return false;

    if (!BatteryIsPresent())
    {
        _JumpToShutdownState("battery removed.");
        return true;
    }

    if (!state_valid || gChargeConfig.critical_percent <= 0 ||
        ChargerIsCharging())
    {
        critical_samples = 0;
        return false;
    }

    if (state->percentage > gChargeConfig.critical_percent)
    {
        critical_samples = 0;
        return false;
    }

    critical_samples++;

    BATTERYDLOG(LOG_WARNING,
        "Battery at %d%%, at or below the critical level of %d%% (%d/%d)",
        state->percentage, gChargeConfig.critical_percent,
        critical_samples, CRITICAL_PERCENT_SAMPLES);

    if (critical_samples < CRITICAL_PERCENT_SAMPLES)
    {
        return false;
    }

    critical_samples = 0;

    BATTERYDLOG(LOG_CRIT, "Battery level is critical... shutting down");
    _JumpToShutdownState("Critical battery levels");

    return true;
}

/**
 * @brief This is the default charging state when the device boots up. In this state if charger is detected
 * while in this state,and battery is authentic with the battery temperature within range, it goes to the
 * "Charging" state. However if battery voltage is below threshold voltage and the charger is not connected,
 * it goes to the "Critical" state.
 *
 * @retval
 */

static ChargeState
StateIdle(nyx_charger_event_t event)
{
    TurnChargingOff("charge state is idle");

    if (!BatteryIsAuthentic() &&  !gChargeConfig.fake_battery)
    {
        return kChargeStateLast;
    }


    if(event & NYX_CHARGER_CONNECTED)
    	return kChargeStateCharging;
    else
    	return kChargeStateLast;
}


/**
 * @brief This is the state in which the device begins shutting down.
 */
static ChargeState
StateShutdown(nyx_charger_event_t event)
{
    static const char kDefaultReason[] = "Critical battery levels";
    const char *reason = gCurrentChargeState.shutdown_reason;
    nyx_battery_status_t state;
    char *report;

    battery_read(&state);

    report = g_strdup_printf(
            "Shutting down with battery"
            "(P: %d%%, T: %d C, C: %d mA, V: %d mV)",
            state.percentage, state.temperature,
            state.current, state.voltage);

    /* report is data, not a format string */
    write_console("%s", report);

    /*
     * This read "if (!reason && !strlen(reason))", which can only ever call
     * strlen on a NULL pointer, and which short-circuits to false for every
     * non-NULL reason - including the empty string ChargeStateReset() sets.
     * So the default was never applied and the device shut down with an empty
     * reason. The default is also static now: it used to be a local array whose
     * address was stored in a global that outlives this frame.
     */
    if (!reason || !*reason)
        reason = kDefaultReason;

    gCurrentChargeState.shutdown_reason = reason;

    MachineShutdown(reason);

    g_free(report);

    return kChargeStateShutdownWait;
}

/**
 * @brief The device stays in this state until it fully shuts down.
 */
static ChargeState
StateShutdownWait(nyx_charger_event_t event)
{
    // The state machine will stick in this state until we actually shut down
    return kChargeStateLast;
}

/**
 * @brief  The device stays in this state as long as it can charge. So as soon as charger is unplugged or
 * battery is removed, it goes to the "idle" state. Also in this state  depending on battery temperature, current,
 * voltage it can go to the "MinTemperatureDone", "MaxTemperatureDone", "MidTempteratureDone", "HighTemperatureDone"
 * states. It goes to the "ChargeComplete" state when the device is fully charger or can also go to "Fault" state
 * if device is overcharging.
 *
 * @retval
 */

static ChargeState
StateCharging(nyx_charger_event_t event)
{
	nyx_battery_status_t state;

    if (!ChargerIsConnected() || !BatteryIsAuthentic())
    {
        return kChargeStateIdle;
    }

    if(event & NYX_CHARGE_COMPLETE)
		return kChargeStateChargeComplete;
	if((event & NYX_CHARGER_DISCONNECTED) || (event & NYX_CHARGE_RESTART) || (event & NYX_BATTERY_TEMPERATURE_LIMIT))
		return kChargeStateIdle;

    battery_read(&state);
    if (!gChargeConfig.disable_overcharge_check && BatteryOverchargeFault(&state))
    {
        return kChargeStateFault;
    }

    if (!TurnChargingON())
    {
        return kChargeStateIdle;
    }

    return kChargeStateLast;
}

static ChargeState
StateChargeComplete(nyx_charger_event_t event)
{
	TurnChargingOff("charge complete");

	g_debug("StateChargeComplete: handling charge complete event");
    if (!ChargerIsConnected() || (event & NYX_CHARGER_DISCONNECTED))
    {
        return kChargeStateIdle;
    }

	if(event & NYX_CHARGE_RESTART) {
    g_debug("StateChargeComplete: charge restart detected, returning to idle");
		return kChargeStateIdle;
    }

    return kChargeStateLast;
}

/**
 * @brief If battery charge capacity exceeds 1.2 times max capacity the device enters this state, and stays in
 * this state until the charger os battery is disconnected.
 *
 * @retval
 */
static ChargeState
StateFault(nyx_charger_event_t event)
{
    TurnChargingOff("charging fault (columbs > 120% ACR).");

    if (!ChargerIsConnected() || !BatteryIsPresent())
    {
        return kChargeStateIdle;
    }

    return kChargeStateLast;
}

/* Public */

/**
* @brief Re-check the battery against the critical thresholds.
*
* Called when nyx reports new readings. ChargingLogicUpdate() runs off charger
* events - a plug, an unplug, a charge-complete - and a device sitting on
* battery generates none of those, so without this the percentage floor below
* would only ever be evaluated on a device that was being plugged in and out.
*
* Deliberately not ChargingLogicUpdate(): that re-runs the whole charge
* decision, and calling TurnChargingON() on every reading would mean a nyx
* enable-charging round trip every few seconds.
*/
void
BatteryLevelCheck(void)
{
    nyx_battery_status_t state;
    bool have_state;

    if (gChargeConfig.skip_battery_check)
    {
        return;
    }

    have_state = battery_read(&state);

    if (CheckCriticalLevels(&state, have_state))
    {
        ChargeStateIterate(NYX_NO_NEW_EVENT);
    }
}

/**
* @brief Called with every change in battery state & charger state.
*
* @param  state
*/
void
ChargingLogicUpdate(nyx_charger_event_t event)
{
    nyx_battery_status_t state;
    bool have_state;

    if (gChargeConfig.skip_battery_check) {
        return;
    }

    if (gChargeConfig.disable_charging)
    {
        BATTERYDLOG(LOG_INFO, "Not making a charge decision because"
            " charging is off in config.");
        return;
    }

    ChargeStateIterate(event);

    /*
     * This read used to be taken at the top of the function and never looked
     * at, because CheckCriticalLevels ignored the struct it was handed. It is
     * taken here, after the state machine has run, and it is now used.
     */
    have_state = battery_read(&state);

    if (CheckCriticalLevels(&state, have_state))
    {
        ChargeStateIterate(event);
    }
}

/*
 * A GSourceFunc, so that g_idle_add() is handed a function of the type it
 * actually calls. This was an int(int) cast to GSourceFunc, with the reason
 * cast straight from int to gpointer - two diagnostics and, on any ABI where
 * the two disagree, a wrong answer.
 */
static gboolean
_battery_check_reason_helper(gpointer data)
{
    int batterycheck = GPOINTER_TO_INT(data);

    switch (batterycheck)
    {
    case BATTERYCHECK_CRITICAL_LOW_BATTERY:
    case BATTERYCHECK_CRITICAL_TEMPERATURE:
		 /* Kernel checks on critical temperature aren't reliable -- pull new readings
			to decide what to do. */
    case BATTERYCHECK_THRESHOLD_CHANGED:
    case BATTERYCHECK_NONE:
    	getNewEvent();
        battery_state_iterate();
        break;
    default:
        break;
    }

    return G_SOURCE_REMOVE;
}

void
BatteryCheckReason(int batterycheck)
{
    g_idle_add(_battery_check_reason_helper, GINT_TO_POINTER(batterycheck));
}

/**
* @brief Reset the charging logic.
* Possible causes of this route could be: Modem reset, etc.
*/
void
ChargingLogicResetError(void)
{
    BATTERYDLOG(LOG_CRIT, "Modem was reset... restarting charge state.");

    ChargeStateInit();
    ChargingLogicUpdate(NYX_NO_NEW_EVENT);
}

/**
 * @brief Return the maximum battery temperature over which the device is shut down.
 */
int batterycheck_maxtemp(void)
{
    if (gChargeConfig.maxtemp)
      return gChargeConfig.maxtemp;
    else
      return BATTERY_MAX_TEMPERATURE_C;
}


static bool
BatteryTemperatureCriticalShutdown(nyx_battery_status_t *batt)
{
    if(battery_ctia_params.battery_crit_max_temp)
    	return (batt->temperature >= battery_ctia_params.battery_crit_max_temp);
    return (batt->temperature >= batterycheck_maxtemp());
}


static bool
BatteryTemperatureLow(nyx_battery_status_t *batt)
{
    if(battery_ctia_params.charge_min_temp_c)
    	return (batt->temperature <= battery_ctia_params.charge_min_temp_c);
    return false;
}


static bool
BatteryTemperatureHigh(nyx_battery_status_t *batt)
{
    if(battery_ctia_params.charge_max_temp_c)
    	return (batt->temperature >= battery_ctia_params.charge_max_temp_c);
    return false;
}

void handle_charger_event(nyx_charger_event_t event)
{
	BATTERYDLOG(LOG_DEBUG,"%s: event : %d",__func__,event);
	if(event & NYX_BATTERY_PRESENT || event & NYX_BATTERY_ABSENT) {
		battery_state_iterate();
		if(gCurrentChargeState.current_state == kChargeStateIdle)
			battery_set_wakeup_percentage(false,false);
	}
	if(event & NYX_BATTERY_CRITICAL_VOLTAGE) {
		if(!ChargerIsCharging())
			_JumpToShutdownState("battery voltage below threshold");
	}
	if(event & NYX_BATTERY_TEMPERATURE_LIMIT) {
		nyx_battery_status_t batt;
		if(!battery_read(&batt))
			return;
		if(BatteryTemperatureCriticalShutdown(&batt))
			_JumpToShutdownState("battery temperature above max allowed");
		else if(BatteryTemperatureHigh(&batt) || BatteryTemperatureLow(&batt))
			TurnChargingOff("charging temperature is above / below the limits allowed");
	}

	ChargingLogicUpdate(event);
}


INIT_FUNC(INIT_FUNC_MIDDLE, ChargeStateInit);

