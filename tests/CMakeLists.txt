# Built only with WEBOS_CONFIG_BUILD_TESTS; host-runnable via run-host-tests.sh
webos_add_test(test_charger_eval
    SOURCES test_charger_eval.c
    LIBRARIES ${GLIB2_LDFLAGS} ${NYXLIB_LDFLAGS})
