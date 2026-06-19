# Makefile — reference PEP harness build.
#   -Wall -Wextra -Werror on all of OUR code. The vendored single-header libs
#   (pep.h, stb_image*.h) are compiled with the same flags plus
#   -Wno-unknown-pragmas, because pep.h uses cosmetic `#pragma region`s that gcc
#   does not recognize; that is the only relaxation, and only for TUs that pull
#   in those headers.
CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -O3 -march=native -DNDEBUG
PEPFLAG := -Wno-unknown-pragmas
LDLIBS  := -lm
B       := build

# -I order: reference headers (src) then root for the vendored stb headers.
INC     := -Isrc -I.

all: $(B)/test_runner $(B)/perf_harness $(B)/build_input

$(B):
	mkdir -p $(B)

# vendored PEP compressor (single TU instantiating pep.h)
$(B)/pep.o: src/pep.c src/pep.h | $(B)
	$(CC) $(CFLAGS) $(PEPFLAG) $(INC) -c src/pep.c -o $@

# shared pre-processing library (load + median-cut quantize + qbin/fnv; owns stb)
$(B)/pepc.o: src/pep_codec.c src/pep_codec.h | $(B)
	$(CC) $(CFLAGS) $(INC) -c src/pep_codec.c -o $@

# build-stage input transform: images/ -> build/q/<name>.qbin (excluded from timing)
$(B)/build_input: performance-test/build_input.c $(B)/pepc.o
	$(CC) $(CFLAGS) $(INC) performance-test/build_input.c $(B)/pepc.o -o $@ $(LDLIBS)

# reference timing harness (includes pep.h -> needs PEPFLAG)
$(B)/perf_harness: performance-test/perf_main.c $(B)/pep.o $(B)/pepc.o
	$(CC) $(CFLAGS) $(PEPFLAG) $(INC) performance-test/perf_main.c $(B)/pep.o $(B)/pepc.o -o $@ $(LDLIBS)

# correctness suite (includes pep.h -> needs PEPFLAG)
$(B)/test_runner: test/test_main.c $(B)/pep.o $(B)/pepc.o
	$(CC) $(CFLAGS) $(PEPFLAG) $(INC) test/test_main.c $(B)/pep.o $(B)/pepc.o -o $@ $(LDLIBS)

# build the quantized input from the committed images (excluded from timing).
IMAGES_DIR ?= images
QDIR       ?= $(B)/q
LIST       ?= $(B)/images.list
MAXCOLORS  ?= 256
.PHONY: input
input: $(B)/build_input
	$(B)/build_input $(IMAGES_DIR) $(QDIR) $(LIST) $(MAXCOLORS)

test: $(B)/test_runner
	$(B)/test_runner

clean:
	rm -rf $(B)

.PHONY: all test clean input
