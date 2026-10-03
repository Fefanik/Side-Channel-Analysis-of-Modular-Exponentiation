CC ?= gcc
COMMON_FLAGS := -Wall -Wextra -Ithird_party/polarssl
SOURCES := src/main.c third_party/polarssl/bignum.c
LDLIBS := -lm

.PHONY: all debug release clean

all: debug release

debug: build/sidechannel-debug

release: build/sidechannel-release

build/sidechannel-debug: $(SOURCES) third_party/polarssl/bignum.h third_party/polarssl/bn_mul.h third_party/polarssl/config.h
	mkdir -p build/debug-data
	$(CC) $(COMMON_FLAGS) -O0 -g $(SOURCES) -o $@ $(LDLIBS)

build/sidechannel-release: $(SOURCES) third_party/polarssl/bignum.h third_party/polarssl/bn_mul.h third_party/polarssl/config.h
	mkdir -p build/release-data
	$(CC) $(COMMON_FLAGS) -O3 $(SOURCES) -o $@ $(LDLIBS)

clean:
	rm -rf build