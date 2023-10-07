#ifndef SCAR_TEST_H
#define SCAR_TEST_H

struct scar_test_result {
	int ok;
	char *file;
	int line;
	char *msg;
};

typedef struct scar_test_result (*scar_test)(void);

struct scar_test_group {
	const char *names;
	scar_test *tests;
};

/// A function which does nothing, but which is called before a test failure.
/// Set a breakpoint on 'scar_breakpoint' in a debugger to pause when a test fails.
void scar_breakpoint(void);

#define TESTGROUP(name, ...) \
static scar_test name ## __tests[] = { __VA_ARGS__, NULL }; \
struct scar_test_group name ## __test_group = { \
	.names = #__VA_ARGS__, \
	.tests = name ## __tests, \
}

#define TEST(name) static struct scar_test_result name(void)

#define OK() return (struct scar_test_result) { 1, NULL, 0, NULL }
#define FAIL(msg) do { \
	scar_breakpoint(); \
	return (struct scar_test_result) { 0, __FILE__, __LINE__, msg }; \
} while (0)
#define ASSERT(x) do { \
	if (!(x)) FAIL("Assertion failed: (" #x ")"); \
} while (0)

#endif
