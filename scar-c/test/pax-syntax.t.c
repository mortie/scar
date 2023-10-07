#include "pax-syntax.h"

#include <string.h>

#include "ioutil.h"
#include "test.h"

TEST(basic_parsing)
{
	const char *pax =
		"20 path=hello world\n"
		"11 size=99\n"
		"16 atime=-45.67\n"
		"16 mtime=100.33\n";
	struct scar_mem_reader mr;
	scar_mem_reader_init(&mr, pax, strlen(pax));

	struct scar_pax_meta meta;
	scar_pax_meta_init_empty(&meta);

	ASSERT(scar_pax_parse(&meta, &mr.r, mr.len) == 0);
	ASSERT(meta.path);
	ASSERT(strcmp(meta.path, "hello world") == 0);
	ASSERT(meta.size == 99);
	ASSERT(meta.atime == -45.67);
	ASSERT(meta.mtime == 100.33);

	scar_pax_meta_destroy(&meta);
	OK();
}

TEST(no_overread)
{
	const char *data =
		"11 size=12\n"
		"Hello World, how are you";

	struct scar_mem_reader mr;
	scar_mem_reader_init(&mr, data, strlen(data));

	struct scar_pax_meta meta;
	scar_pax_meta_init_empty(&meta);

	ASSERT(scar_pax_parse(&meta, &mr.r, 11) == 0);
	ASSERT(meta.size == 12);

	char buf[24];
	ASSERT(mr.r.read(&mr.r, buf, sizeof(buf)) == sizeof(buf));
	ASSERT(strncmp(buf, "Hello World, how are you", sizeof(buf)) == 0);

	OK();
}

TEST(no_overread_block_aligned)
{
	char blocks[1024];
	strcpy(blocks, "512 path=");
	for (int i = 9; i < 511; ++i) {
		blocks[i] = 'a';
	}
	blocks[511] = '\n';

	strcpy(&blocks[512], "hello world");

	struct scar_mem_reader mr;
	scar_mem_reader_init(&mr, blocks, 1024);

	struct scar_pax_meta meta;
	scar_pax_meta_init_empty(&meta);

	ASSERT(scar_pax_parse(&meta, &mr.r, 512) >= 0);
	ASSERT(meta.path);

	char str[12];
	ASSERT(mr.r.read(&mr.r, str, 12) == 12);
	ASSERT(strncmp(str, "hello world", 12) == 0);

	scar_pax_meta_destroy(&meta);
	OK();
}

TESTGROUP(pax_syntax, basic_parsing, no_overread, no_overread_block_aligned);
