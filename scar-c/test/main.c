#include "test.h"

#include <stdio.h>
#include <string.h>

#define TEST_GROUPS \
	X(compression_gzip) \
	X(ioutil_block_reader) \
	X(ioutil_mem) \
	X(pax_syntax) \
//

#define X(name) extern struct scar_test_group name ## __test_group;
TEST_GROUPS
#undef X

void scar_breakpoint(void)
{
	// This function only exists to work as a breakpoint in debuggers.
}

static int count_tests(struct scar_test_group tg)
{
	int num;
	for (num = 0; tg.tests[num]; ++num);
	return num;
}

static int run_group(int *base, const char *groupname, struct scar_test_group tg)
{
	char namebuf[128];
	const char *names = tg.names;

	int numsuccess = 0;
	for (int i = 0; tg.tests[i]; ++i) {
		char *end = strchr(names, ',');
		if (end) {
			size_t len = (size_t)(end - names);
			memcpy(namebuf, names, len);
			namebuf[len] = '\0';
			names += len + 2;
		} else {
			strcpy(namebuf, names);
		}

		struct scar_test_context tctx = {
			.id = *base,
			.groupname = groupname,
			.testname = namebuf,
		};

		int result = tg.tests[i](tctx);
		if (result >= 0) {
			numsuccess += 1;
		}

		*base += 1;
	}

	return numsuccess;
}

int main(void)
{
	int numtests = 0;
#define X(name) numtests += count_tests(name ## __test_group);
	TEST_GROUPS
#undef X

	printf("TAP version 14\n");
	printf("1..%i\n", numtests);

	int base = 1;
	int numsuccess = 0;
#define X(name) numsuccess += run_group(&base, #name, name ## __test_group);
	TEST_GROUPS
#undef X

	printf("\n# %i/%i tests succeeded.\n", numsuccess, numtests);
	if (numsuccess == numtests) {
		return 0;
	} else {
		return 1;
	}
}
