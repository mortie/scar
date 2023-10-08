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
		int ret = scar_pax_read_meta(&global, &meta, &in.r);
		if (ret < 0) {
			scar_pax_meta_destroy(&meta);
			fprintf(stderr, "Error reading pax meta\n");
			return 1;
		} else if (ret == 0) {
			return 0;
		}

		scar_pax_meta_print(&meta, &out.w);
		scar_pax_meta_destroy(&meta);
	}

	scar_pax_meta_destroy(&global);
}
