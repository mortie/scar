#include <scar/scar-writer.h>
#include <scar/ioutil.h>
#include <scar/compression.h>
#include <stdio.h>

int main(void)
{
	struct scar_file out;
	scar_file_init(&out, stdout);

	struct scar_compression comp;
	scar_compression_init_gzip(&comp);

	struct scar_writer *sw = scar_writer_create(&out.w, &comp, 6);
	if (!sw) {
		fprintf(stderr, "Failed to create scar writer\n");
		return 1;
	}

	struct scar_pax_meta meta;
	scar_pax_meta_init_directory(&meta, "/foo/");
	scar_writer_write_entry(sw, &meta, NULL);
	scar_pax_meta_destroy(&meta);

	struct scar_mem_reader content;
	scar_mem_reader_init(&content, "Hello World", 11);
	scar_pax_meta_init_file(&meta, "/foo/bar.txt", content.len);
	scar_pax_meta_destroy(&meta);

	scar_writer_finish(sw);
	scar_writer_free(sw);
}
