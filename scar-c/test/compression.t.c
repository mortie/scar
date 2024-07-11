#include "compression.h"

#include <string.h>
#include <stdlib.h>

#include "ioutil.h"
#include "test.h"

const char story[] =
	"Helloooo! This is your captain speaking.\n"
	"We unfortunately have to report that this boat is about to take off.\n"
	"We've had rogue engineers install unauthorized helium balloons "
	"for quite some time now,\n"
	"and though they have been discovered and taken care of,\n"
	"the buoyant force from said balloons are now enough to counteract "
	"the force of gravity.\n"
	"The practical consequences is that we've started floating in the air.\n"
	"We have been able to contact the nearest air traffic control center,\n"
	"and we are happy to inform you that we are cleared for landing.\n"
	"It is not quite clear yet when exactly this landing will take place.\n"
	"Current wind conditions means that we will be approaching "
	"Gatwick Airport\n"
	"in approximately 5 hours.\n"
	"Please stay seated until we reach our calculated cruising altitude\n"
	"of approximately 50 feet.\n"
	"Our technicians are currently installing fasten seatbelt signs.\n"
	"If installed in time, the fasten seatbelt signs "
	"will switch on once we are ready to\n"
	"go in for landing, or if we encounter unexpected turbulence.\n";

static int test_roundtrip(
	struct scar_test_context scar_test_ctx,
	struct scar_compression *comp
) {
	struct scar_mem_writer mw;
	scar_mem_writer_init(&mw);

	// Compress data into the memory writer
	struct scar_compressor *compressor = comp->create_compressor(&mw.w, 6);
	ASSERT2(
		compressor->w.write(&compressor->w, story, sizeof(story) - 1), ==,
		(scar_ssize)sizeof(story) - 1);
	ASSERT2(compressor->finish(compressor), ==, 0);
	comp->destroy_compressor(compressor);

	// Create a memory reader from the buffer with compressed data
	struct scar_mem_reader mr;
	scar_mem_reader_init(&mr, mw.buf, mw.len);

	// Remember the mem writer's buffer so we can free it later,
	// then re-initialize it for the output of the decompressor
	void *buf = mw.buf;
	scar_mem_writer_init(&mw);

	// Decompress the compressed data into the memory writer
	struct scar_decompressor *decompressor =
		comp->create_decompressor(&mr.r);
	ASSERT2(
		scar_io_copy(&decompressor->r, &mw.w), ==,
		(scar_ssize)sizeof(story) - 1);
	comp->destroy_decompressor(decompressor);

	ASSERT2(mw.len, ==, sizeof(story) - 1);
	ASSERT2(memcmp(mw.buf, story, sizeof(story) - 1), ==, 0);

	free(buf);
	free(mw.buf);

	OK();
}

static int test_roundtrip_chunked(
	struct scar_test_context scar_test_ctx,
	struct scar_compression *comp
) {
	struct scar_mem_writer mw;
	scar_mem_writer_init(&mw);

	struct scar_compressor *compressor = comp->create_compressor(&mw.w, 6);

	for (int i = 0; i < 10; ++i) {
		ASSERT2(compressor->w.write(
			&compressor->w, "Hello World\n", 12), ==, 12);
	}

	ASSERT2(compressor->finish(compressor), ==, 0);
	comp->destroy_compressor(compressor);

	// Create a memory reader from the buffer with compressed data
	struct scar_mem_reader mr;
	scar_mem_reader_init(&mr, mw.buf, mw.len);

	// Remember the mem writer's buffer so we can free it later,
	// then re-initialize it for the output of the decompressor
	void *buf = mw.buf;
	scar_mem_writer_init(&mw);

	struct scar_decompressor *decompressor = comp->create_decompressor(&mr.r);
	ASSERT2(scar_io_copy(&decompressor->r, &mw.w), ==, 12 * 10);
	comp->destroy_decompressor(decompressor);

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

#define DEFTEST(fn, name) TEST(fn ## _ ## name) \
{ \
	struct scar_compression comp; \
	scar_compression_init_ ## name(&comp); \
	return test_ ## fn(scar_test_ctx, &comp); \
}

#define X(xname) \
DEFTEST(roundtrip, xname) \
DEFTEST(roundtrip_chunked, xname) \
//
SCAR_COMPRESSOR_NAMES
#undef X

TESTGROUP(compression,
	roundtrip_plain, roundtrip_chunked_plain,
	roundtrip_gzip, roundtrip_chunked_gzip);
