#include "ioutil.h"

#include "test.h"

TEST(repeated_consume) {
	unsigned char text[4000];
	for (size_t i = 0; i < sizeof(text); ++i) {
		text[i] = '0' + (i % 10);
	}

	struct scar_mem_reader mr;
	scar_mem_reader_init(&mr, text, sizeof(text));

	struct scar_block_reader br;
	scar_block_reader_init(&br, &mr.r);

	for (size_t i = 0; i < sizeof(text); ++i) {
		ASSERT(!br.eof);
		ASSERT(!br.error);
		ASSERT(br.next == text[i]);
		scar_block_reader_consume(&br);
	}

	ASSERT(br.eof);
	ASSERT(!br.error);

	OK();
}

TESTGROUP(ioutil_block_reader, repeated_consume);
