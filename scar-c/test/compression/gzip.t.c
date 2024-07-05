#include "compression.h"

#include <string.h>
#include <stdlib.h>

#include "ioutil.h"
#include "test.h"

TEST(roundtrip)
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
		"If installed in time, the fasten seatbelt signs "
		"will switch on once we are ready to\n"
		"go in for landing, or if we encounter unexpected turbulence.\n";

	struct scar_compression gzip;
	scar_compression_init_gzip(&gzip);

	struct scar_mem_writer mw;
	scar_mem_writer_init(&mw);

	// Compress data into the memory writer
	struct scar_compressor *compressor = gzip.create_compressor(&mw.w, 6);
	ASSERT2(
		compressor->w.write(&compressor->w, data, sizeof(data) - 1), ==,
		(scar_ssize)sizeof(data) - 1);
	ASSERT2(compressor->finish(compressor), ==, 0);
	gzip.destroy_compressor(compressor);

	// Create a memory reader from the buffer with compressed data
	struct scar_mem_reader mr;
	scar_mem_reader_init(&mr, mw.buf, mw.len);

	// Remember the mem writer's buffer so we can free it later,
	// then re-initialize it for the output of the decompressor
	void *buf = mw.buf;
	scar_mem_writer_init(&mw);

	// Decompress the compressed data into the memory writer
	struct scar_decompressor *decompressor = gzip.create_decompressor(&mr.r);
	ASSERT2(scar_io_copy(&decompressor->r, &mw.w), ==, (scar_ssize)sizeof(data) - 1);
	gzip.destroy_decompressor(decompressor);

	ASSERT2(mw.len, ==, sizeof(data) - 1);
	ASSERT2(memcmp(mw.buf, data, sizeof(data) - 1), ==, 0);

	free(buf);
	free(mw.buf);

	OK();
}

TEST(roundtrip_chunked)
{
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

	// Create a memory reader from the buffer with compressed data
	struct scar_mem_reader mr;
	scar_mem_reader_init(&mr, mw.buf, mw.len);

	// Remember the mem writer's buffer so we can free it later,
	// then re-initialize it for the output of the decompressor
	void *buf = mw.buf;
	scar_mem_writer_init(&mw);

	struct scar_decompressor *decompressor = gzip.create_decompressor(&mr.r);
	ASSERT2(scar_io_copy(&decompressor->r, &mw.w), ==, 12 * 10);
	gzip.destroy_decompressor(decompressor);

	ASSERT2(mw.len, ==, (size_t)(12 * 10));
	char *str = mw.buf;
	for (int i = 0; i < 10; ++i) {
		ASSERT2(memcmp(str, "Hello World\n", 12), ==, 0);
		str += 12;
	}

	free(buf);
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
	scar_ssize r =
		decompressor->r.read(&decompressor->r, decbuf, sizeof(decbuf));
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

TESTGROUP(
	compression_gzip,
	roundtrip, roundtrip_chunked, decompress, decompress_chunked);
