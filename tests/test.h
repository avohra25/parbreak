/* Minimal test harness: CHECK records failures, RUN executes a test. */
#ifndef PB_TEST_H
#define PB_TEST_H
#include <math.h>
#include <stdio.h>

extern int pb_test_failures;
extern int pb_test_checks;

#define CHECK(cond)                                                              \
    do {                                                                         \
        pb_test_checks++;                                                        \
        if (!(cond)) {                                                           \
            pb_test_failures++;                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(fabs((double)(a) - (double)(b)) <= (eps))

#define RUN(fn)                                                                  \
    do {                                                                         \
        int before_ = pb_test_failures;                                          \
        fn();                                                                    \
        printf("%s %s\n", pb_test_failures == before_ ? "ok  " : "FAIL", #fn);   \
    } while (0)

#endif
