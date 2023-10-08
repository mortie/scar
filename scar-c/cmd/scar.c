#include <scar/pax.h>
#include <scar/ioutil.h>

int main(void)
{
	struct scar_pax_meta meta;
	scar_pax_meta_init_file(&meta, "Hello");
	meta.atime = 100.440000001;
	meta.type = SCAR_FT_FILE;

	struct scar_file out;
	scar_file_init(&out, stdout);
	scar_pax_write_meta(&meta, &out.w);
	scar_pax_meta_destroy(&meta);

	scar_pax_write_end(&out.w);
}
