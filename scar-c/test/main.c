#include "test.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define TEST_GROUPS \
	X(pax_syntax) \
//

#define X(name) extern struct scar_test_group name ## __test_group;
TEST_GROUPS
#undef X

void scar_breakpoint(void)
{
}

static size_t count_tests(struct scar_test_group tg)
{
	size_t num;
	for (num = 0; tg.tests[num]; ++num);
	return num;
}

static size_t run_group(size_t *base, const char *groupname, struct scar_test_group tg)
{
	char namebuf[128];
	const char *names = tg.names;

	size_t numsuccess = 0;
	for (size_t i = 0; tg.tests[i]; ++i) {
		char *end = strchr(names, ',');
		if (end) {
			size_t len = (size_t)(end - names);
			memcpy(namebuf, names, len);
			namebuf[len] = '\0';
			names += len + 2;
		} else {
			strcpy(namebuf, names);
		}

		struct scar_test_result result = tg.tests[i]();
		if (result.ok) {
			numsuccess += 1;
			printf("ok %zu - %s :: %s\n", *base, groupname, namebuf);
		} else {
			printf("not ok %zu - %s :: %s\n", *base, groupname, namebuf);
			printf("# where: %s:%d\n", result.file, result.line);
			printf("# message: %s\n", result.msg);
		}

		*base += 1;
	}

	return numsuccess;
}

int main(void)
{
	size_t numtests = 0;
#define X(name) numtests += count_tests(name ## __test_group);
	TEST_GROUPS
#undef X

	printf("TAP version 14\n");
	printf("1..%zu\n", numtests);

	size_t base = 1;
	size_t numsuccess = 0;
#define X(name) numsuccess += run_group(&base, #name, name ## __test_group);
	TEST_GROUPS
#undef X

	printf("\n# %zu/%zu tests succeeded.\n", numsuccess, numtests);
}
