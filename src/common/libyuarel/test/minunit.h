#include "src/common/libtap/tap.h"

/* macro to print out the header for a new group of tests */
#define mu_group(name) diag("%s", name)

/* macro for asserting a statement */
#define mu_assert(message, test) do { \
	ok (test, message); \
	} while (0)

/* macro for asserting a statement without printing it unless it is a failure */
#define mu_silent_assert(message, test) do { \
		if (!(test)) { \
			diag ("%s", message); \
			return (unsigned char *)message; \
		} \
	} while (0)

/* run a test function and return result */
#define mu_run_test(test) do { \
		unsigned char *message = test(); tests_run++; \
		if (message) { return message; } \
	} while (0)
