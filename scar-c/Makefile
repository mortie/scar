OUT ?= build

MESON ?= meson
NINJA ?= ninja

MESON_FLAGS ?=
NINJA_FLAGS ?=

.PHONY: all
all: $(OUT)/scar

$(OUT)/build.ninja:
	$(MESON) setup $(MESON_FLAGS) $(OUT)

.PHONY: $(OUT)/scar
$(OUT)/scar: $(OUT)/build.ninja
	$(NINJA) -C $(OUT) $(NINJA_FLAGS) scar

.PHONY: $(OUT)/test-scar
$(OUT)/test-scar: $(OUT)/build.ninja
	$(NINJA) -C $(OUT) $(NINJA_FLAGS) test-scar

.PHONY: setup
setup: $(OUT)/build.ninja

.PHONY: check
check: $(OUT)/test-scar
	$(OUT)/test-scar

.PHONY: check-lldb
check-lldb: $(OUT)/test-scar
	lldb -o 'breakpoint set -n scar_breakpoint -C finish' -o 'run' $<

.PHONY: check-gdb
check-gdb: $(OUT)/test-scar
	gdb -ex 'break scar_breakpoint' -ex 'run' $<

.PHONY: check-valgrind
check-valgrind: $(OUT)/test-scar
	valgrind --leak-check=full $<

.PHONY: clean
clean:
	$(NINJA) -C build clean

.PHONY: cleanall
cleanall:
	rm -rf $(OUT)
