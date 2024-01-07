OUT ?= build

LIBSCAR_SRCS := \
	src/compression/gzip.c \
	src/ioutil.c \
	src/pax-meta.c \
	src/pax-syntax.c \
	src/pax.c \
	src/scar-writer.c \
#

SCAR_TEST_SRCS := \
	test/compression/gzip.t.c \
	test/ioutil.t.c \
	test/main.c \
	test/pax-syntax.t.c \
	test/pax-syntax.t.c \
#

SANITIZE ?=

ifneq ($(SANITIZE),)
	CFLAGS += -fsanitize=$(SANITIZE)
	LDFLAGS += -fsanitize=$(SANITIZE)
endif

CFLAGS := $(CFLAGS) \
	-Wall -Wextra -Wconversion -Wpedantic \
	-std=c99 -g -DSCAR_TRACE_ERROR -fPIC \
#

BIN_LDFLAGS := $(LDFLAGS) -L. -lscar

ifeq ($(shell uname),Linux)
BIN_LDFLAGS += -Wl,-rpath='$$ORIGIN'
endif

.PHONY: all
all: libscar.so scar scar-test scar-test-writer pax-analyze tar-analyze

.PHONY: clean
clean:
	rm -rf $(OUT)
	rm -f libscar.so scar-test scar pax-analyze tar-analyze scar-write

.PHONY: check
check: scar-test
	./scar-test

.PHONY: check-lldb
check-lldb: scar-test
	lldb -o 'breakpoint set -n scar_breakpoint -C finish' -o 'run' ./scar-test

.PHONY: check-gdb
check-gdb: scar-test
	gdb -ex 'break scar_breakpoint' -ex 'run' ./scar-test

libscar.so: $(patsubst %,$(OUT)/%.o,$(LIBSCAR_SRCS))
	$(CC) -shared -o $@ $^ $(LDFLAGS) -lm -lz

scar-test: $(patsubst %,$(OUT)/%.o,$(SCAR_TEST_SRCS)) libscar.so
	$(CC) -o $@ $(filter %.o,$^) $(BIN_LDFLAGS)

scar-test-writer: $(OUT)/cmd/scar-test-writer.c.o libscar.so
	$(CC) -o $@ $< $(BIN_LDFLAGS)

scar: $(OUT)/cmd/scar.c.o libscar.so
	$(CC) -o $@ $< $(BIN_LDFLAGS)

pax-analyze: $(OUT)/cmd/pax-analyze.c.o libscar.so
	$(CC) -o $@ $< $(BIN_LDFLAGS)

tar-analyze: $(OUT)/cmd/tar-analyze.c.o libscar.so
	$(CC) -o $@ $< $(BIN_LDFLAGS)

$(OUT)/src/%.c.o: src/%.c
	@mkdir -p $(@D)
	$(CC) -c -o $@ -MMD $(CFLAGS) -Iinclude/scar $<
$(OUT)/src/%.c.d: $(OUT)/src/%.c.o

$(OUT)/test/%.c.o: test/%.c
	@mkdir -p $(@D)
	$(CC) -c -o $@ -MMD $(CFLAGS) -Iinclude/scar -Itest  $<
$(OUT)/test/%.c.d: $(OUT)/test/%.c.o

$(OUT)/cmd/%.c.o: cmd/%.c
	@mkdir -p $(@D)
	$(CC) -c -o $@ -MMD $(CFLAGS) -Iinclude $<
$(OUT)/cmd/%.c.d: $(OUT)/cmd/%.c.o

-include $(patsubst %,$(OUT)/%.d,$(LIBSCAR_SRCS) \
	$(SCAR_TEST_SRCS) cmd/scar.c cmd/pax-analyze.c cmd/tar-analyze.c)
