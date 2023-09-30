OUT ?= build

LIBSCAR_SRCS := \
	src/ioutil.c \
	src/pax.c \
#

CFLAGS += -Wall -Wextra -std=c99

.PHONY: all
all: libscar.so test

.PHONY: clean
clean:
	rm -rf $(OUT)
	rm -f libscar.so test

libscar.so: $(patsubst %,$(OUT)/%.o,$(LIBSCAR_SRCS))
	$(CC) -shared -o $@ $^ $(LDFLAGS)

test: $(OUT)/cmd/test.c.o libscar.so
	$(CC) -o $@ $< -L. -lscar

$(OUT)/src/%.c.o: src/%.c
	@mkdir -p $(@D)
	$(CC) -c -o $@ -MMD $(CFLAGS) -Iinclude/scar $<
$(OUT)/src/%.c.d: $(OUT)/src/%.c.o

$(OUT)/cmd/%.c.o: cmd/%.c
	@mkdir -p $(@D)
	$(CC) -c -o $@ -MMD $(CFLAGS) -Iinclude $<
$(OUT)/bin/%.c.d: $(OUT)/bin/%.c.o

-include $(patsubst %,$(OUT)/%.d,$(LIBSCAR_SRCS)) $(OUT)/cmd/test.c.d
