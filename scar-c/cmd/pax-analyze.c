#include <scar/pax.h>
#include <scar/ioutil.h>
#include <stdio.h>

int main(void)
{
	struct scar_file in;
	scar_file_init(&in, stdin);

	struct scar_file out;
	scar_file_init(&out, stdout);

	struct scar_pax_meta global;
	scar_pax_meta_init_empty(&global);

	struct scar_pax_meta meta;

	while (1) {
		if (scar_pax_meta_read(&global, &meta, &in.r)) {
			fprintf(stderr, "Error reading pax meta\n");
			return 1;
		}

		scar_pax_meta_print(&meta, &out.w);
	}
}
