/* Eclipse SSH - minimal test framework (single header, MIT).
 * Macros: MU_TEST, MU_RUN_TEST, MU_REPORT_SUMMARY, mu_check, mu_check_msg,
 * mu_assert_string_eq, mu_check_int_eq, MU_SUITE_CONFIGURE,
 * MU_TEST_SUITE_SETUP, MU_TEST_SUITE_TEARDOWN.
 */
#ifndef EC_MINUNIT_H
#define EC_MINUNIT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int minunit_run = 0;
static int minunit_fail = 0;
static int minunit_check_failed = 0;
static void (*mu_setup_fn)(void) = NULL;
static void (*mu_teardown_fn)(void) = NULL;

#define MU__SAFE_BLOCK(block) do { block } while (0)

#define MU_TEST(method_name) static void method_name(void)

#define MU_SUITE_CONFIGURE(setup, teardown) \
    MU__SAFE_BLOCK( (mu_setup_fn) = (setup); (mu_teardown_fn) = (teardown); )

#define MU_TEST_SUITE_SETUP() static void test_suite_setup(void)
#define MU_TEST_SUITE_TEARDOWN() static void test_suite_teardown(void)

#define mu_check(test) \
    MU__SAFE_BLOCK( \
        minunit_run++; \
        if (!(test)) { \
            minunit_fail++; \
            minunit_check_failed = 1; \
            printf("%s:%d: FAIL: %s\n", __FILE__, __LINE__, #test); \
            return; \
        } \
        minunit_check_failed = 0; )

#define mu_check_msg(test, msg) \
    MU__SAFE_BLOCK( \
        minunit_run++; \
        if (!(test)) { \
            minunit_fail++; \
            printf("%s:%d: FAIL: %s (%s)\n", __FILE__, __LINE__, #test, (msg) ? (msg) : "-"); \
            return; \
        } )

#define mu_fail(message) \
    MU__SAFE_BLOCK( \
        minunit_run++; \
        minunit_fail++; \
        printf("%s:%d: FAIL: %s\n", __FILE__, __LINE__, message); \
        return; )

#define mu_assert_string_eq(expected, actual) \
    MU__SAFE_BLOCK( \
        minunit_run++; \
        const char* mu_e_ = (expected); \
        const char* mu_a_ = (actual); \
        if (!mu_e_ || !mu_a_ || strcmp(mu_e_, mu_a_) != 0) { \
            minunit_fail++; \
            printf("%s:%d: FAIL: strings differ: expected '%s' got '%s'\n", \
                   __FILE__, __LINE__, mu_e_ ? mu_e_ : "(null)", mu_a_ ? mu_a_ : "(null)"); \
            return; \
        } )

#define mu_check_int_eq(expected, actual) \
    MU__SAFE_BLOCK( \
        minunit_run++; \
        long mu_e_ = (long)(expected); \
        long mu_a_ = (long)(actual); \
        if (mu_e_ != mu_a_) { \
            minunit_fail++; \
            printf("%s:%d: FAIL: ints differ: expected %ld got %ld\n", \
                   __FILE__, __LINE__, mu_e_, mu_a_); \
            return; \
        } )

#define MU_RUN_TEST(method_name) \
    MU__SAFE_BLOCK( \
        printf("RUN  %s\n", #method_name); \
        if (mu_setup_fn) mu_setup_fn(); \
        method_name(); \
        if (mu_teardown_fn && !minunit_check_failed) mu_teardown_fn(); )

#define MU_REPORT_SUMMARY() \
    MU__SAFE_BLOCK( \
        printf("\n%d tests, %d failures\n", minunit_run, minunit_fail); )

#endif /* EC_MINUNIT_H */
