#include "ioutil.h"

#include <stdlib.h>

#include "io.h"
#include "test.h"

TEST(mem_reader_read)
{
	struct scar_mem_reader mr;
	scar_mem_reader_init(&mr, "Hello World", 11);

	char buf[4];
	ASSERT2(mr.r.read(&mr.r, buf, sizeof(buf)), ==, 4);
	ASSERT_STREQ_N(buf, "Hell", 4);
	ASSERT2(mr.r.read(&mr.r, buf, sizeof(buf)), ==, 4);
	ASSERT_STREQ_N(buf, "o Wo", 4);
	ASSERT2(mr.r.read(&mr.r, buf, sizeof(buf)), ==, 3);
	ASSERT_STREQ_N(buf, "rld", 3);

	OK();
}

TEST(mem_reader_seek)
{
	struct scar_mem_reader mr;
	scar_mem_reader_init(&mr, "Hello World", 11);

	char buf[3];
	ASSERT2(mr.r.read(&mr.r, buf, sizeof(buf)), ==, 3);
	ASSERT_STREQ_N(buf, "Hel", 3);
	ASSERT2(mr.r.read(&mr.r, buf, sizeof(buf)), ==, 3);
	ASSERT_STREQ_N(buf, "lo ", 3);
	ASSERT2(mr.s.tell(&mr.s), ==, 6);

	ASSERT2(mr.s.seek(&mr.s, 0, SCAR_SEEK_START), ==, 0);
	ASSERT2(mr.r.read(&mr.r, buf, sizeof(buf)), ==, 3);
	ASSERT_STREQ_N(buf, "Hel", 3);

	ASSERT2(mr.s.seek(&mr.s, -3, SCAR_SEEK_END), ==, 0);
	ASSERT2(mr.r.read(&mr.r, buf, sizeof(buf)), ==, 3);
	ASSERT_STREQ_N(buf, "rld", 3);

	ASSERT2(mr.s.seek(&mr.s, -4, SCAR_SEEK_CURRENT), ==, 0);
	ASSERT2(mr.r.read(&mr.r, buf, sizeof(buf)), ==, 3);
	ASSERT_STREQ_N(buf, "orl", 3);

	OK();
}

TEST(mem_writer_write)
{
	struct scar_mem_writer mw;
	scar_mem_writer_init(&mw);

	ASSERT2(mw.w.write(&mw.w, "Hello", 5), ==, 5);
	ASSERT_STREQ_N(mw.buf, "Hello", 5);

	for (int i = 0; i < 4; ++i) {
		ASSERT2(mw.w.write(&mw.w, "Hello", 5), ==, 5);
	}
	ASSERT_STREQ_N(mw.buf, "HelloHelloHelloHelloHello", 5 * 5);

	free(mw.buf);
	OK();
}

TESTGROUP(ioutil_mem, mem_reader_read, mem_reader_seek, mem_writer_write);
