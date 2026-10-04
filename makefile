CFLAGS += -O0 -Wall -Wextra -g -MMD
LDFLAGS += -lm -g

ifeq ($(OS),Windows_NT)
	MSYSINSTALLDIR := $(patsubst %/,%,$(shell cd /;pwd -W))
	ROOTDIR := $(MSYSINSTALLDIR)$(CURDIR)
else
	ROOTDIR := $(CURDIR)
endif

# temlib lives next to this project
TEMBLIB := $(ROOTDIR)/../temlib

CFLAGS += -I$(ROOTDIR)/include
CFLAGS += -I$(ROOTDIR)/libs
CFLAGS += -I$(TEMBLIB)
CFLAGS += -I$(TEMBLIB)/include
CFLAGS += -I$(TEMBLIB)/libs

TEMBLIB_LIBS := -lbacktrace -lpthread -lz
TEMBLIB_ARCHIVE := $(TEMBLIB)/build/libtemlib.a

SRCS_C := $(shell cd src;find . -name '*.c')
OBJS := $(patsubst %.c,obj/%.o,$(SRCS_C))
DEPS := $(patsubst %.c,obj/%.d,$(SRCS_C))

MAIN_C := $(shell cd mains;find . -name '*.c')
MAIN_OBJS := $(patsubst %.c,obj/mains/%.o,$(MAIN_C))
DEPS_MAIN := $(patsubst %.c,obj/mains/%.d,$(MAIN_C))

STATIC_LIB := build/libgriefprot.a

all: $(STATIC_LIB) test

-include $(DEPS)
-include $(DEPS_MAIN)

obj/%.o : src/%.c
	mkdir -p $(shell echo $@ | sed -r "s/(.+)\/.+/\1/")
	$(CC) $(CFLAGS) -c $< -o $@

obj/mains/%.o : mains/%.c
	mkdir -p $(shell echo $@ | sed -r "s/(.+)\/.+/\1/")
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: static
static: $(STATIC_LIB)
$(STATIC_LIB): $(OBJS)
	mkdir -p build
	$(AR) rcs $@ $^

MAIN_NAMES := $(basename $(notdir $(MAIN_C)))

# one target per main, e.g. `make test` builds build/test
define MAIN_template =
.PHONY: $(1)
$(1): $(STATIC_LIB) obj/mains/$(1).o
	mkdir -p build
	$$(CC) $$(CFLAGS) -o build/$(1) obj/mains/$(1).o $(STATIC_LIB) $(TEMBLIB_ARCHIVE) $$(LDFLAGS) $$(TEMBLIB_LIBS)
endef
$(foreach m,$(MAIN_NAMES),$(eval $(call MAIN_template,$(m))))

test: test

# --- Java host shim ---------------------------------------------------------
#
# The FFM surface lives in java/native and is built as a shared library so the
# Paper plugin can load it. It is kept out of $(STATIC_LIB) on purpose: the shim
# is a boundary layer, and a plugin shipping it should not also pull the raw C
# API into its own symbol namespace.
#
# `make abi`         build the shared library and its tests
# `make abi-test`    build and run the shim tests

ABI_DIR := java/native
ABI_C := $(wildcard $(ABI_DIR)/*.c)
ABI_MAIN := gp_abi_test

ABI_OBJ := $(patsubst $(ABI_DIR)/%.c,obj/abi/%.o,$(filter-out $(ABI_DIR)/$(ABI_MAIN).c,$(ABI_C)))
ABI_TEST_OBJ := obj/abi/$(ABI_MAIN).o

CFLAGS += -I$(ROOTDIR)/$(ABI_DIR)

ifeq ($(OS),Windows_NT)
	SHARED_EXT := dll
	SHARED_PREFIX :=
else
	SHARED_EXT := so
	SHARED_PREFIX := lib
endif

SHARED_LIB := build/$(SHARED_PREFIX)griefprot_ffi.$(SHARED_EXT)

.PHONY: abi
abi: $(SHARED_LIB)

# Every gp_abi_* the header promises has to be reachable by name, because FFM
# looks symbols up by string and a missing one only fails at the moment Java
# calls it. Checking at build time turns that into a build failure.
#
# The extraction runs in the recipe rather than via $(shell) so the list cannot
# come back empty and quietly pass everything. An empty list is a hard error.
.PHONY: abi-exports
abi-exports: $(SHARED_LIB)
	@if [ "$(OS)" = "Windows_NT" ]; then \
		exported=$$(objdump -p $(SHARED_LIB) | grep -o 'gp_abi_[A-Za-z_][A-Za-z_]*' | sort -u); \
	else \
		exported=$$(nm -D --defined-only $(SHARED_LIB) | grep -o 'gp_abi_[A-Za-z_][A-Za-z_]*' | sort -u); \
	fi; \
	if [ -z "$$exported" ]; then \
		echo "could not read exports from $(SHARED_LIB); refusing to pass"; exit 1; \
	fi; \
	missing=0; \
	for f in $$(grep -o 'gp_abi_[A-Za-z_][A-Za-z_]*' $(ABI_DIR)/gp_abi.h | sort -u); do \
		echo "$$exported" | grep -qx "$$f" || { echo "MISSING EXPORT: $$f"; missing=1; }; \
	done; \
	if [ $$missing -eq 0 ]; then \
		echo "all header functions exported ($$(echo "$$exported" | wc -l) gp_abi_* symbols)"; \
	else \
		exit 1; \
	fi

$(SHARED_LIB): $(ABI_OBJ) $(OBJS) $(TEMBLIB_ARCHIVE)
	mkdir -p build
	$(CC) $(CFLAGS) -shared -o $@ $(ABI_OBJ) $(OBJS) $(TEMBLIB_ARCHIVE) $(LDFLAGS) $(TEMBLIB_LIBS)

obj/abi/%.o : $(ABI_DIR)/%.c
	mkdir -p obj/abi
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: abi-test
abi-test: build/$(ABI_MAIN)
	./build/$(ABI_MAIN)

build/$(ABI_MAIN): $(ABI_TEST_OBJ) $(ABI_OBJ) $(OBJS)
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(ABI_TEST_OBJ) $(ABI_OBJ) $(OBJS) $(TEMBLIB_ARCHIVE) $(LDFLAGS) $(TEMBLIB_LIBS)

clean:
	rm -rf build/*
	rm -rf obj/*

