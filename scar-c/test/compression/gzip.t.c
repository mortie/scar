#include "compression.h"

#include "ioutil.h"
#include "test.h"

#include <string.h>
#include <stdlib.h>

TEST(compress)
{
	char data[] =
		"Helloooo! This is your captain speaking.\n"
		"We unfortunately have to report that this boat is about to take off.\n"
		"We've had rogue engineers install unauthorized helium balloons for quite some time now,\n"
		"and though they have been discovered and taken care of,\n"
		"the buoyant force from said balloons are now enough to counteract the force of gravity.\n"
		"The practical consequences is that we've started floating in the air.\n"
		"We have been able to contact the nearest air traffic control center,\n"
		"and we are happy to inform you that we are cleared for landing.\n"
		"It is not quite clear yet when exactly this landing will take place.\n"
		"Current wind conditions means that we will be approaching Gatwick Airport\n"
		"in approximately 5 hours.\n"
		"Please stay seated until we reach our calculated cruising altitude\n"
		"of approximately 50 feet.\n"
		"Our technicians are currently installing fasten seatbelt signs.\n"
		"If installed in time, the fasten seatbelt signs will switch on once we are ready to\n"
		"go in for landing, or if we encounter unexpected turbulence.\n";

	unsigned char compressed_data[] = {
		0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x6d, 0x52,
		0xc1, 0x8e, 0xd5, 0x30, 0x0c, 0xbc, 0xf7, 0x2b, 0xcc, 0x89, 0xcb, 0xd3,
		0x13, 0x17, 0x3e, 0x00, 0x71, 0x00, 0x4e, 0x70, 0x58, 0x89, 0xb3, 0x9b,
		0xba, 0xad, 0xb5, 0x79, 0x49, 0x37, 0x71, 0xf6, 0x6d, 0xf9, 0x7a, 0xc6,
		0x69, 0x97, 0x45, 0x88, 0xaa, 0x6a, 0xab, 0xc6, 0x9e, 0x19, 0xcf, 0xf8,
		0xab, 0xc4, 0x98, 0x71, 0xbd, 0xa3, 0x87, 0x55, 0x2b, 0xe1, 0xde, 0x73,
		0x2b, 0x14, 0x78, 0x33, 0xd6, 0x44, 0x75, 0x13, 0x7e, 0xd4, 0xb4, 0x5c,
		0x87, 0x9f, 0x42, 0x2d, 0xcd, 0xb9, 0x58, 0x4b, 0x6c, 0x12, 0x77, 0x5a,
		0xf9, 0x59, 0xc8, 0x32, 0x15, 0xd9, 0xf0, 0x97, 0x6c, 0x65, 0x7f, 0x00,
		0x60, 0xcc, 0xf8, 0xc2, 0x9b, 0xc7, 0xdc, 0xcc, 0x2b, 0x8c, 0x1f, 0x85,
		0xf2, 0x3c, 0x3b, 0xc8, 0x7b, 0x34, 0xad, 0x3c, 0x51, 0xc9, 0x4b, 0x13,
		0x92, 0xb4, 0x68, 0x12, 0x29, 0x20, 0x4e, 0xd5, 0x38, 0x46, 0x70, 0x70,
		0xb3, 0x35, 0x17, 0xfd, 0x25, 0x13, 0xad, 0x12, 0xb5, 0xdd, 0x68, 0x64,
		0xd7, 0x98, 0x2a, 0x81, 0x9e, 0x9e, 0x9a, 0x9a, 0x50, 0xcd, 0x37, 0x90,
		0x2b, 0x1e, 0x29, 0xdf, 0x2f, 0x03, 0xa7, 0x09, 0xdc, 0xb9, 0x2d, 0x2b,
		0x5e, 0x72, 0x6a, 0x1b, 0x45, 0x12, 0x4d, 0x5a, 0x43, 0x7e, 0x96, 0x02,
		0xb4, 0x5e, 0x04, 0x29, 0x09, 0xd3, 0x15, 0x17, 0x74, 0x19, 0x50, 0x4c,
		0x63, 0xcb, 0x3b, 0x27, 0x73, 0xf0, 0x20, 0x34, 0x97, 0x7c, 0xa3, 0xca,
		0x3a, 0xbd, 0xb1, 0x7a, 0x31, 0x58, 0x20, 0xf6, 0x20, 0xc8, 0x14, 0x72,
		0x4b, 0x26, 0x85, 0x83, 0x4f, 0x2c, 0x67, 0x67, 0x9e, 0x69, 0x29, 0xfc,
		0xac, 0xb6, 0x5f, 0x87, 0x07, 0xfc, 0xdd, 0xfc, 0x5c, 0x03, 0x47, 0x94,
		0xa7, 0x2a, 0x4f, 0x4d, 0x52, 0x90, 0xee, 0x70, 0xb7, 0xea, 0xde, 0xad,
		0xc0, 0xd0, 0xc5, 0xa0, 0x6d, 0x8e, 0x30, 0x0d, 0x3e, 0xc3, 0x87, 0x0e,
		0xc9, 0x5a, 0xba, 0xe5, 0x6f, 0x83, 0xf0, 0x18, 0xe5, 0xe0, 0x4e, 0xf6,
		0x4a, 0x9c, 0x04, 0xda, 0xaa, 0x79, 0x35, 0x59, 0xe1, 0x79, 0xd6, 0xd0,
		0xcf, 0x4b, 0x06, 0xa9, 0xb8, 0xc4, 0xc3, 0x9a, 0xbb, 0xf4, 0x21, 0x56,
		0xde, 0xb6, 0xdd, 0x31, 0xd4, 0x83, 0xbc, 0x79, 0xd2, 0xaf, 0x5a, 0xfa,
		0x79, 0x88, 0x8e, 0x37, 0x75, 0x9b, 0x23, 0xfa, 0x7a, 0xee, 0xdf, 0x7a,
		0x96, 0x29, 0xdb, 0xe9, 0x7c, 0x2f, 0xa2, 0x5d, 0xd0, 0xb5, 0x42, 0x96,
		0xbc, 0x40, 0x0c, 0xb6, 0xa1, 0x27, 0x7f, 0x36, 0xd1, 0x5d, 0x91, 0x64,
		0x4f, 0x7d, 0x8b, 0x1c, 0xe4, 0x3a, 0x7c, 0x6e, 0xa5, 0x40, 0x0f, 0x0e,
		0xa0, 0x06, 0x0a, 0x27, 0x35, 0x75, 0x6b, 0x6f, 0xc2, 0xe9, 0x8f, 0x1f,
		0x47, 0xdb, 0x08, 0x2d, 0xdb, 0x56, 0x32, 0x87, 0xd5, 0xa1, 0xbe, 0xb0,
		0xdd, 0x35, 0x3c, 0xd2, 0x27, 0x2d, 0xbe, 0x66, 0x03, 0xfc, 0xe9, 0xc7,
		0x2f, 0x7a, 0x3b, 0xd6, 0xf0, 0x23, 0x21, 0xf7, 0x52, 0xaf, 0xc3, 0x0f,
		0x08, 0xab, 0xdd, 0xd1, 0x9d, 0xaa, 0xb0, 0xbb, 0x8a, 0x94, 0x34, 0x3a,
		0x72, 0x11, 0xc0, 0xd1, 0xb1, 0xd8, 0x31, 0xb4, 0xd8, 0x4f, 0x43, 0x69,
		0x5a, 0x9d, 0x83, 0xa3, 0xa9, 0xb5, 0x49, 0x06, 0x44, 0xf8, 0x0f, 0xf8,
		0x07, 0x9a, 0x45, 0xec, 0x3a, 0x7c, 0x47, 0xab, 0x49, 0x58, 0x93, 0x06,
		0xe5, 0x73, 0x27, 0xc2, 0x31, 0x14, 0xca, 0xce, 0xe5, 0x75, 0xac, 0x99,
		0xab, 0xc1, 0x16, 0x17, 0x30, 0x4a, 0x34, 0xaa, 0xba, 0x24, 0x88, 0xfb,
		0x36, 0xbf, 0x16, 0x81, 0xd8, 0x33, 0xc6, 0xea, 0x5e, 0x8e, 0xe5, 0xf9,
		0x5f, 0xc3, 0x61, 0x45, 0xbd, 0xab, 0xb9, 0xec, 0x84, 0x1b, 0x0b, 0x76,
		0xa6, 0x84, 0x59, 0x26, 0x4f, 0x71, 0x58, 0x3c, 0xc8, 0xbf, 0xc3, 0xba,
		0x10, 0xbe, 0x75, 0xf6, 0x42, 0xec, 0xda, 0xb1, 0xa3, 0xf0, 0x40, 0x5e,
		0x36, 0x09, 0x3e, 0xb0, 0xb5, 0x32, 0xb6, 0xe8, 0x6b, 0x78, 0x1d, 0x7e,
		0x03, 0x0e, 0xf3, 0x74, 0x31, 0xf8, 0x03, 0x00, 0x00,
	};

	struct scar_compression gzip;
	scar_compression_init_gzip(&gzip);

	struct scar_mem_writer mw;
	scar_mem_writer_init(&mw);

	struct scar_compressor *compressor = gzip.create_compressor(&mw.w, 6);
	ASSERT2(
		compressor->w.write(&compressor->w, data, sizeof(data) - 1), ==,
		(scar_ssize)sizeof(data) - 1);
	ASSERT2(compressor->finish(compressor), ==, 0);
	gzip.destroy_compressor(compressor);

	ASSERT2(mw.len, ==, sizeof(compressed_data));
	ASSERT2(memcmp(mw.buf, compressed_data, mw.len), ==, 0);
	free(mw.buf);

	OK();
}

TEST(compress_chunked)
{
	unsigned char compressed_data[] = {
		0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xf3, 0x48,
		0xcd, 0xc9, 0xc9, 0x57, 0x08, 0xcf, 0x2f, 0xca, 0x49, 0xe1, 0xf2, 0xa0,
		0x23, 0x1b, 0x00, 0xc2, 0x7d, 0x35, 0x15, 0x78, 0x00, 0x00, 0x00,
	};

	struct scar_compression gzip;
	scar_compression_init_gzip(&gzip);

	struct scar_mem_writer mw;
	scar_mem_writer_init(&mw);

	struct scar_compressor *compressor = gzip.create_compressor(&mw.w, 6);

	for (int i = 0; i < 10; ++i) {
		ASSERT2(compressor->w.write(&compressor->w, "Hello World\n", 12), ==, 12);
	}

	ASSERT2(compressor->finish(compressor), ==, 0);
	gzip.destroy_compressor(compressor);

	ASSERT2(mw.len, ==, sizeof(compressed_data));
	ASSERT2(memcmp(mw.buf, compressed_data, mw.len), ==, 0);
	free(mw.buf);

	OK();
}

TEST(decompress)
{
	char data[] = "Hello World\n";

	unsigned char compressed_data[] = {
		0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xf3, 0x48,
		0xcd, 0xc9, 0xc9, 0x57, 0x08, 0xcf, 0x2f, 0xca, 0x49, 0xe1, 0x02, 0x00,
		0xe3, 0xe5, 0x95, 0xb0, 0x0c, 0x00, 0x00, 0x00,
	};

	struct scar_compression gzip;
	scar_compression_init_gzip(&gzip);

	struct scar_mem_reader mr;
	scar_mem_reader_init(&mr, compressed_data, sizeof(compressed_data));

	struct scar_decompressor *decompressor = gzip.create_decompressor(&mr.r);

	char decbuf[512];
	scar_ssize r = decompressor->r.read(&decompressor->r, decbuf, sizeof(decbuf));
	ASSERT2(r, ==, (scar_ssize)sizeof(data) - 1);
	ASSERT2(memcmp(decbuf, data, sizeof(data) - 1), ==, 0);

	gzip.destroy_decompressor(decompressor);

	OK();
}

TEST(decompress_chunked)
{
	unsigned char compressed_data[] = {
		0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xf3, 0x48,
		0xcd, 0xc9, 0xc9, 0x57, 0x08, 0xcf, 0x2f, 0xca, 0x49, 0xe1, 0xf2, 0xa0,
		0x23, 0x1b, 0x00, 0xc2, 0x7d, 0x35, 0x15, 0x78, 0x00, 0x00, 0x00,
	};

	struct scar_compression gzip;
	scar_compression_init_gzip(&gzip);

	struct scar_mem_reader mr;
	scar_mem_reader_init(&mr, compressed_data, sizeof(compressed_data));

	struct scar_decompressor *decompressor = gzip.create_decompressor(&mr.r);

	char buf[12];
	for (int i = 0; i < 10; ++i) {
		ASSERT2(decompressor->r.read(&decompressor->r, buf, 12), ==, 12);
		ASSERT2(memcmp(buf, "Hello World\n", 12), ==, 0);
	}

	gzip.destroy_decompressor(decompressor);

	OK();
}

TESTGROUP(compression_gzip, compress, compress_chunked, decompress, decompress_chunked);
