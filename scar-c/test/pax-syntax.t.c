#include "pax-syntax.h"

#include <string.h>

#include "ioutil.h"
#include "test.h"

TEST(pax_parsing)
{
	const char *pax =
		"20 path=hello world\n"
		"11 size=99\n";
	struct scar_mem_reader mr;
	scar_mem_reader_init(&mr, pax, strlen(pax));

	struct scar_pax_meta meta;
	scar_pax_meta_init_empty(&meta);

	ASSERT(scar_pax_parse(&meta, &mr.r, mr.len));
	ASSERT(meta.path);
	ASSERT(strcmp(meta.path, "hello world") == 0);
	ASSERT(meta.size == 99);

	OK();
}

TESTGROUP(pax_syntax, pax_parsing);
