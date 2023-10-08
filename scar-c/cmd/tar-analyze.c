#include <scar/ustar.h>
#include <scar/ioutil.h>
#include <stdio.h>
#include <stdint.h>

static uint64_t read_u64(const unsigned char *block, struct scar_ustar_field field)
{
	const unsigned char *text = &block[field.start];
	uint64_t num = 0;
	for (size_t i = 0; i < field.length; ++i) {
		unsigned char ch = text[i];
		if (ch < '0' || ch > '7') {
			return num;
		}

		num *= 8;
		num += ch - '0';
	}

	return num;
}

int main(void)
{
	struct scar_file in;
	scar_file_init(&in, stdin);

	unsigned char block[512];

	while (1) {
		if (in.r.read(&in.r, block, 512) < 512) {
			return 1;
		}

		if (block[SCAR_UST_VERSION.start] == 0) {
			break;
		}

		printf("Entry:\n");
		printf("  name: '%.*s'\n", (int)SCAR_UST_NAME.length, &block[SCAR_UST_NAME.start]);
		printf("  mode: %.*s\n", (int)SCAR_UST_MODE.length, &block[SCAR_UST_MODE.start]);
		printf("  uid: %.*s\n", (int)SCAR_UST_UID.length, &block[SCAR_UST_UID.start]);
		printf("  gid: %.*s\n", (int)SCAR_UST_GID.length, &block[SCAR_UST_GID.start]);
		printf("  size: %.*s\n", (int)SCAR_UST_SIZE.length, &block[SCAR_UST_SIZE.start]);
		printf("  mtime: %.*s\n", (int)SCAR_UST_MTIME.length, &block[SCAR_UST_MTIME.start]);
		printf("  chksum: %.*s\n", (int)SCAR_UST_CHKSUM.length, &block[SCAR_UST_CHKSUM.start]);
		printf("  typeflag: %c\n", block[SCAR_UST_TYPEFLAG.start]);
		printf("  linkname: '%.*s'\n", (int)SCAR_UST_LINKNAME.length, &block[SCAR_UST_LINKNAME.start]);
		printf("  magic: %.*s\n", (int)SCAR_UST_MAGIC.length, &block[SCAR_UST_MAGIC.start]);
		printf("  version: %.*s\n", (int)SCAR_UST_VERSION.length, &block[SCAR_UST_VERSION.start]);
		printf("  uname: %.*s\n", (int)SCAR_UST_UNAME.length, &block[SCAR_UST_UNAME.start]);
		printf("  gname: %.*s\n", (int)SCAR_UST_GNAME.length, &block[SCAR_UST_GNAME.start]);
		printf("  devmajor: %.*s\n", (int)SCAR_UST_DEVMAJOR.length, &block[SCAR_UST_DEVMAJOR.start]);
		printf("  devminor: %.*s\n", (int)SCAR_UST_DEVMINOR.length, &block[SCAR_UST_DEVMINOR.start]);
		printf("  prefix: '%.*s'\n", (int)SCAR_UST_PREFIX.length, &block[SCAR_UST_PREFIX.start]);

		int64_t size = (int64_t)read_u64(block, SCAR_UST_SIZE);
		while (size > 0) {
			if (in.r.read(&in.r, block, 512) < 512) {
				return 1;
			}

			size -= 512;
		}
	}
}
