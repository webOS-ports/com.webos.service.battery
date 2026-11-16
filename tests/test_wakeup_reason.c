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
 * wakeup_reason.h picks the first wakeup reason a kernel offers. Each test
 * builds the three sysfs files in a temporary directory.
 * Host-runnable: tests/run-host-tests.sh
 */

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "../batteryd/charging/wakeup_reason.h"

enum { PALM, ANDROID, MAINLINE, NFILES };

typedef struct
{
    gchar *dir;
    gchar *path[NFILES];
    const char *files[NFILES];
} Fixture;

static void fixture_setup(Fixture *f, gconstpointer data)
{
    static const char *names[NFILES] = {
        "wakeup_event_list", "last_resume_reason", "pm_wakeup_irq"
    };
    int i;

    (void) data;
    f->dir = g_dir_make_tmp("wakeup-reason-XXXXXX", NULL);
    g_assert_nonnull(f->dir);
    for (i = 0; i < NFILES; i++)
    {
        f->path[i] = g_build_filename(f->dir, names[i], NULL);
        f->files[i] = f->path[i];
    }
}

static void fixture_teardown(Fixture *f, gconstpointer data)
{
    int i;

    (void) data;
    for (i = 0; i < NFILES; i++)
    {
        g_unlink(f->path[i]);
        g_free(f->path[i]);
    }
    g_rmdir(f->dir);
    g_free(f->dir);
}

static void put(const char *path, const char *contents)
{
    g_assert_true(g_file_set_contents(path, contents, -1, NULL));
}

/* no kernel interface at all, as on the 3.x Samsung and TouchPad kernels */
static void test_none(Fixture *f, gconstpointer data)
{
    char buf[64] = "stale";

    (void) data;
    g_assert_cmpint(wakeup_reason_read(f->files, NFILES, buf, sizeof(buf)), ==, -1);
    g_assert_cmpstr(buf, ==, "");
}

/* an Android kernel that reports: one line per interrupt, newline stripped */
static void test_android(Fixture *f, gconstpointer data)
{
    char buf[64];

    (void) data;
    put(f->path[ANDROID], "200 mt6358-irq\n201 mtk-eint\n");
    g_assert_cmpint(wakeup_reason_read(f->files, NFILES, buf, sizeof(buf)), ==, ANDROID);
    g_assert_cmpstr(buf, ==, "200 mt6358-irq\n201 mtk-eint");
}

/* an Android kernel whose platform does not report (the Titan): file present
 * but empty, so the mainline one behind it is used if there is one */
static void test_android_empty(Fixture *f, gconstpointer data)
{
    char buf[64];

    (void) data;
    put(f->path[ANDROID], "\n");
    g_assert_cmpint(wakeup_reason_read(f->files, NFILES, buf, sizeof(buf)), ==, -1);
    put(f->path[MAINLINE], "42\n");
    g_assert_cmpint(wakeup_reason_read(f->files, NFILES, buf, sizeof(buf)), ==, MAINLINE);
    g_assert_cmpstr(buf, ==, "42");
}

/* the order is fixed: Palm's list wins over the others */
static void test_order(Fixture *f, gconstpointer data)
{
    char buf[64];

    (void) data;
    put(f->path[PALM], "[ 1.0 ] rtc () rtc (1)\n");
    put(f->path[ANDROID], "200 mt6358-irq\n");
    put(f->path[MAINLINE], "42\n");
    g_assert_cmpint(wakeup_reason_read(f->files, NFILES, buf, sizeof(buf)), ==, PALM);
    g_assert_cmpstr(buf, ==, "[ 1.0 ] rtc () rtc (1)");
}

/* a file that exists but cannot be read is skipped; stands in for
 * pm_wakeup_irq failing with ENODATA */
static void test_unreadable(Fixture *f, gconstpointer data)
{
    char buf[64];

    (void) data;
    put(f->path[ANDROID], "200 mt6358-irq\n");
    put(f->path[PALM], "[ 1.0 ] rtc () rtc (1)\n");
    g_assert_cmpint(g_chmod(f->path[PALM], 0), ==, 0);
    if (access(f->path[PALM], R_OK) == 0)
    {
        g_test_skip("running as root, a mode of 0 does not stop reads");
        return;
    }
    g_assert_cmpint(wakeup_reason_read(f->files, NFILES, buf, sizeof(buf)), ==, ANDROID);
    g_assert_cmpstr(buf, ==, "200 mt6358-irq");
}

/* a reason longer than the buffer is cut, and still terminated */
static void test_truncated(Fixture *f, gconstpointer data)
{
    char buf[8];

    (void) data;
    put(f->path[ANDROID], "  200 mt6358-irq  \n");
    g_assert_cmpint(wakeup_reason_read(f->files, NFILES, buf, sizeof(buf)), ==, ANDROID);
    g_assert_cmpstr(buf, ==, "200 mt6");
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
#define ADD(name, fn) \
    g_test_add("/wakeup-reason/" name, Fixture, NULL, fixture_setup, fn, fixture_teardown)
    ADD("none", test_none);
    ADD("android", test_android);
    ADD("android-empty", test_android_empty);
    ADD("order", test_order);
    ADD("unreadable", test_unreadable);
    ADD("truncated", test_truncated);
#undef ADD
    return g_test_run();
}
