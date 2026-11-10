#include <stdio.h>

#include "test.h"

int pb_test_failures = 0;
int pb_test_checks = 0;

void run_core_tests(void);
void run_layout_tests(void);
void run_hyphen_tests(void);

int main(void) {
    run_core_tests();
    run_layout_tests();
    run_hyphen_tests();
    printf("\n%d checks, %d failures\n", pb_test_checks, pb_test_failures);
    return pb_test_failures ? 1 : 0;
}
