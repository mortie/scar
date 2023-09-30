#include <scar/ioutil.h>
#include <scar/pax.h>

int main()
{
	struct scar_file f;
	scar_file_init(&f, stdout);

	struct scar_pax_meta meta;
	scar_pax_meta_init_empty(&meta);
	scar_pax_meta_print(&meta, &f.w);

	fflush(stdout);
}
