OUT ?= build

LIBSCAR_SRCS := \
	src/ioutil.c \
	src/pax.c \
	src/pax-syntax.c \
#

SCAR_TEST_SRCS := \
	test/main.c \
	test/pax-syntax.t.c \
#

CFLAGS += -Wall -Wextra -Wconversion -Wpedantic -std=c99 -g -DSCAR_TRACE_ERROR

.PHONY: all
all: libscar.so scar scar-test pax-analyze

.PHONY: clean
clean:
	rm -rf $(OUT)
	rm -f libscar.so scar-test scar pax-analyze

.PHONY: check
check: scar-test
	./scar-test

.PHONY: check-lldb
check-lldb: scar-test
	lldb -o 'breakpoint set -n scar_breakpoint -C finish' -o 'run' ./scar-test

libscar.so: $(patsubst %,$(OUT)/%.o,$(LIBSCAR_SRCS))
	$(CC) -shared -o $@ $^ $(LDFLAGS)

scar-test: $(patsubst %,$(OUT)/%.o,$(SCAR_TEST_SRCS)) libscar.so
	$(CC) -o $@ $(filter %.o,$^) -L. -lscar

scar: $(OUT)/cmd/scar.c.o libscar.so
	$(CC) -o $@ $< -L. -lscar

pax-analyze: $(OUT)/cmd/pax-analyze.c.o libscar.so
	$(CC) -o $@ $< -L. -lscar

$(OUT)/src/%.c.o: src/%.c
	@mkdir -p $(@D)
	$(CC) -c -o $@ -MMD $(CFLAGS) -Iinclude/scar $<
$(OUT)/src/%.c.d: $(OUT)/src/%.c.o

$(OUT)/test/%.c.o: test/%.c
	@mkdir -p $(@D)
	$(CC) -c -o $@ -MMD $(CFLAGS) -Iinclude/scar $<
$(OUT)/test/%.c.d: $(OUT)/test/%.c.o

$(OUT)/cmd/%.c.o: cmd/%.c
	@mkdir -p $(@D)
	$(CC) -c -o $@ -MMD $(CFLAGS) -Iinclude $<
$(OUT)/cmd/%.c.d: $(OUT)/cmd/%.c.o

-include $(patsubst %,$(OUT)/%.d,$(LIBSCAR_SRCS) $(SCAR_TEST_SRCS) cmd/scar.c)