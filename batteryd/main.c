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


#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <glib.h>
#include <pthread.h>
#include <stdbool.h>
#include <getopt.h>
#include <stdlib.h>

#include <luna-service2/lunaservice.h>

#include "init.h"
#include "batteryd_debug.h"
#include "timesaver.h"
#include "batteryd_config.h"
#include "logging.h"

static GMainLoop *mainloop = NULL;
static LSHandle* private_sh = NULL;

bool batteryd_debug = false;

void
term_handler(int signal)
{
    g_main_loop_quit(mainloop);
}


GMainContext *
GetMainLoopContext(void)
{
    return g_main_loop_get_context(mainloop);
}

LSHandle *
GetLunaServiceHandle(void)
{
    return private_sh;
}

int
main(int argc, char **argv)
{
    bool retVal;
    int exit_status = 0;

    /*
     * Every one of these was parsed into a local and then dropped on the floor:
     * only --debug had any effect at all, and only because it is read a few
     * lines below. The five that have somewhere to go are written into
     * gChargeConfig before TheOneInit() runs, so the modules see them. The
     * three that did not - --visual-leds-suspend, --verbose-syslog and
     * --error-on-critical - are gone rather than left in --help promising
     * something the daemon does not do.
     */
    gboolean debug = FALSE;
    gboolean fake_battery = FALSE;
    gboolean fasthalt = FALSE;
    gint maxtemp = 0;
    gint temprate = 0;

    GOptionEntry entries[] = {
        {"debug", 'd', 0, G_OPTION_ARG_NONE, &debug, "turn debug logging on", NULL},
        {"use-fake-battery", 'b', 0, G_OPTION_ARG_NONE, &fake_battery, "Use fake battery", NULL},
        {"maxtemp", 'M', 0, G_OPTION_ARG_INT, &maxtemp, "Set maximum temperature before shutdown (default 60)", NULL},
        {"temprate", 'T', 0, G_OPTION_ARG_INT, &temprate, "Expected maxiumum temperature slew rate (default 12)", NULL},
        {"fasthalt", 'F', 0, G_OPTION_ARG_NONE, &fasthalt, "On overtemp, shut down quickly not cleanly", NULL},
        { NULL }
    };

    GError *error = NULL;
    GOptionContext *ctx;
    ctx = g_option_context_new(" - battery daemon");
    g_option_context_add_main_entries(ctx, entries, NULL);
    if (!g_option_context_parse(ctx, &argc, &argv, &error)) {
        g_critical("option parsing failed: %s", error->message);
        if (error)
            g_error_free(error);
        g_option_context_free(ctx);
        exit(1);
    }

    g_option_context_free(ctx);

    // FIXME integrate this into TheOneInit()
    LOGInit();
    LOGSetHandler(LOGSyslog);

    if (debug) {
        batteryd_debug = true;

        LOGSetLevel(G_LOG_LEVEL_DEBUG);
        LOGSetHandler(LOGGlibLog);
    }

    /*
     * config_init() runs as an INIT_FUNC_FIRST hook and can still override
     * these from com.webos.service.battery.conf, which is the existing
     * precedence and is left alone.
     */
    gChargeConfig.debug        = debug ? true : false;
    gChargeConfig.fake_battery = fake_battery ? true : false;
    gChargeConfig.fasthalt     = fasthalt ? 1 : 0;
    gChargeConfig.maxtemp      = maxtemp;
    gChargeConfig.temprate     = temprate;
   
    signal(SIGTERM, term_handler);
    signal(SIGINT, term_handler);

    /*if (!g_thread_supported ()) g_thread_init ();*/

    mainloop = g_main_loop_new(NULL, FALSE);

    /**
     *  initialize the lunaservice and we want it before all the init
     *  stuff happening.
     */
    LSError lserror;
    LSErrorInit(&lserror);

    retVal = LSRegister("com.webos.service.battery", &private_sh, &lserror);
    if (!retVal)
    {
        goto ls_error;
    }

    retVal = LSGmainAttach(private_sh, mainloop, &lserror);
    if (!retVal)
    {
        goto ls_error;
    }

    /**
     * Calls the init functions of all the modules in priority order.
     */
    TheOneInit();

    g_main_loop_run(mainloop);

end:
    // Cleanup initialization hooks before freeing mainloop
    TheOneCleanup();
    
    g_main_loop_unref(mainloop);

    // save time before quitting...
    timesaver_save();

    return exit_status;
ls_error:
    g_critical("Fatal - Could not initialize batteryd.  Is LunaService Down?. %s",
        lserror.message);
    LSErrorFree(&lserror);
    exit_status = 1;
    goto end;
}
