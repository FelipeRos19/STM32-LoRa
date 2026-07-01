#ifndef CRYPTO_TEST_H
#define CRYPTO_TEST_H

/*
 * TEMPORARY bring-up test for the crypto module. Exercises the full API on
 * real hardware and prints PASS/FAIL over USART2. There is no radio here --
 * this only validates the cryptography end to end. Remove this file (and its
 * call site / CMake entry) once the communication layer takes over.
 *
 * Must be called from task context (the crypto APIs take a FreeRTOS mutex).
 * Returns 0 if every sub-test passed, -1 otherwise.
 */
int crypto_test_run(void);

#endif /* CRYPTO_TEST_H */
