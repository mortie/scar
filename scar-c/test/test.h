#ifndef SCAR_TEST_H
#define SCAR_TEST_H

#if defined(__GNUC__)
#define HAS_TYPEOF
#define TYPEOF __typeof__
#endif

/// A test result, returned from functions declared with the 'TEST' macro.
struct scar_test_result {
	int ok;
	char *file;
	int line;
	char *msg;
};

/// A function pointer to a test function.
typedef struct scar_test_result (*scar_test)(void);

/// Represents a test group. Declare a test group using the 'TESTGROUP' macro.
struct scar_test_group {
	const char *names;
	scar_test *tests;
};

/// A function which does nothing, but which is called before a test failure.
/// Set a breakpoint on 'scar_breakpoint' in a debugger to pause when a test fails.
void scar_breakpoint(void);

/// Define a "test group". Usually occurs exactly once per .t.c file.
#define TESTGROUP(name, ...) \
static scar_test name ## __tests[] = { __VA_ARGS__, NULL }; \
struct scar_test_group name ## __test_group = { \
	.names = #__VA_ARGS__, \
	.tests = name ## __tests, \
}

/// Define a test. The 'name' should occur in the test file's TESTGROUP.
#define TEST(name) static struct scar_test_result name(void)

/// Return success from the test.
#define OK() return (struct scar_test_result) { 1, NULL, 0, NULL }

/// Return failure from the test.
#define FAIL(msg) do { \
	scar_breakpoint(); \
	return (struct scar_test_result) { 0, __FILE__, __LINE__, msg }; \
} while (0)

/// Return failure from the test if !(x).
#define ASSERT(x) do { \
	if (!(x)) FAIL("Assertion failed: (" #x ")"); \
} while (0)

#ifdef HAS_TYPEOF
/// ASSERT2(a, op, b) is the same as ASSERT(a op b),
/// except that the operands 'a' and 'b' are defined as the variables 'op_a' and 'op_b'.
/// This 
#define ASSERT2(a, op, b) do { \
	TYPEOF(a) op_a = a; \
	TYPEOF(b) op_b = b; \
	if (!(op_a op op_b)) { \
		scar_breakpoint(); \
		FAIL("Assertion failed: (" #a ") " #op " (" #b ")"); \
	} \
} while (0)
#else
/// On compilers without a 'TYPEOF' macro, 'ASSERT2(a, op, b)' is turned into 'ASSERT((a) op (b))'.
#define ASSERT2(a, op, b) ASSERT((a) op (b))
#endif

#endif
