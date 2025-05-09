# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2010-2014 Intel Corporation

# binary name
APP = lw4o6_tester

CC = g++

# all source are stored in SRCS-y main.cpp Throughput.cpp
SRCS-y := 

ifeq ($(TARGET),tp)
    SRCS-y := main-tp.cpp Throughput.cpp
endif

ifeq ($(TARGET),lat)
    SRCS-y := main-lat.cpp latency.cpp Throughput.cpp
endif

ifeq ($(TARGET),pdv)
    SRCS-y := main-pdv.cpp pdv.cpp Throughput.cpp
endif


PKGCONF ?= pkg-config

# Build using pkg-config variables if possible
ifneq ($(shell $(PKGCONF) --exists libdpdk && echo 0),0)
$(error "no installation of DPDK found")
endif

all: shared
.PHONY: shared static
shared: build/$(APP)-shared
	ln -sf $(APP)-shared build/$(APP)
static: build/$(APP)-static
	ln -sf $(APP)-static build/$(APP)

PC_FILE := $(shell $(PKGCONF) --path libdpdk 2>/dev/null)
#for performance
CFLAGS += -O3 $(shell $(PKGCONF) --cflags libdpdk) 
#for debug
#CFLAGS += -g $(shell $(PKGCONF) --cflags libdpdk)
LDFLAGS_SHARED = $(shell $(PKGCONF) --libs libdpdk)
LDFLAGS_STATIC = $(shell $(PKGCONF) --static --libs libdpdk)

ifeq ($(MAKECMDGOALS),static)
# check for broken pkg-config
ifeq ($(shell echo $(LDFLAGS_STATIC) | grep 'whole-archive.*l:lib.*no-whole-archive'),)
$(warning "pkg-config output list does not contain drivers between 'whole-archive'/'no-whole-archive' flags.")
$(error "Cannot generate statically-linked binaries with this version of pkg-config")
endif
endif

CFLAGS += -DALLOW_EXPERIMENTAL_API
CFLAGS += --std=c++20

LDFLAGS += -lnuma -latomic

build/$(APP)-shared: $(SRCS-y) Makefile $(PC_FILE) | build
	$(CC) $(CFLAGS) $(SRCS-y) -o $@ $(LDFLAGS) $(LDFLAGS_SHARED)

build/$(APP)-static: $(SRCS-y) Makefile $(PC_FILE) | build
	$(CC) $(CFLAGS) $(SRCS-y) -o $@ $(LDFLAGS) $(LDFLAGS_STATIC)

build:
	@mkdir -p $@

.PHONY: clean
clean:
	rm -f build/$(APP) build/$(APP)-static build/$(APP)-shared
	test -d build && rmdir -p build || true