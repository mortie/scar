#ifndef SCAR_TEST_H
#define SCAR_TEST_H

#include <string.h>

#if defined(__GNUC__)
#define HAS_TYPEOF
#define TYPEOF __typeof__
#endif

struct scar_test_context;

/// A function pointer to a test function.
typedef int (*scar_test)(struct scar_test_context);

/// Represents a test group. Declare a test group using the 'TESTGROUP' macro.
struct scar_test_group {
	const char *names;
	scar_test *tests;
};

/// Represents a test context. Passed to the test function.
struct scar_test_context {
	int id;
	const char *groupname;
	const char *testname;
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
#define TEST(name) static int name(struct scar_test_context scar_test_ctx)

/// Return success from the test.
#define OK() do { \
	printf("ok %d - %s :: %s\n", \
		scar_test_ctx.id, scar_test_ctx.groupname, scar_test_ctx.testname); \
	(void)scar_test_ctx; \
	return 0; \
} while (0)

/// Return failure from the test.
#define FAIL(...) do { \
	printf("not ok %d - %s :: %s\n", \
		scar_test_ctx.id, scar_test_ctx.groupname, scar_test_ctx.testname); \
	printf("# At %s:%d\n", __FILE__, __LINE__); \
	printf("# " __VA_ARGS__); \
	scar_breakpoint(); \
	return -1; \
} while (0)

/// Return failure from the test if !(x).
#define ASSERT(x) do { \
	if (!(x)) { \
		FAIL("Assertion failed: (%s)\n", #x); \
	} \
} while (0)

#ifdef HAS_TYPEOF
/// ASSERT2(a, op, b) is the same as ASSERT(a op b),
/// except that the operands 'a' and 'b' are defined as the variables 'op_a' and 'op_b'.
/// This 
#define ASSERT2(a, op, b) do { \
	TYPEOF(a) op_a = a; \
	TYPEOF(b) op_b = b; \
	if (!(op_a op op_b)) { \
		FAIL("Assertion failed: (%s) %s (%s)\n", #a, #op, #b); \
	} \
} while (0)
#else
/// On compilers without a 'TYPEOF' macro, 'ASSERT2(a, op, b)' is turned into 'ASSERT((a) op (b))'.
#define ASSERT2(a, op, b) ASSERT((a) op (b))
#endif

#define ASSERT_STREQ(a, b) do { \
	const char *op_a = (a); \
	const char *op_b = (b); \
	if (strcmp(op_a, op_b) != 0) { \
		FAIL("Assertion failed: Strings not equal: \"%s\", \"%s\"\n", op_a, op_b); \
	} \
} while (0)

#define ASSERT_STREQ_N(a, b, n) do { \
	const char *op_a = (a); \
	const char *op_b = (b); \
	if (strncmp(op_a, op_b, n) != 0) { \
		FAIL("Assertion failed: Strings not equal: \"%.*s\", \"%.*s\"\n", \
			n, op_a, n, op_b); \
	} \
} while (0)

#endif
