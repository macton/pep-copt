# viz/Makefile — builds the quality visualizer. Separate from the reference and
# optimized builds; touches no harness target. Run from the project root:
#   make -C viz            # build build/contact_sheet
#   make -C viz shots      # build + render sheets for the committed images
CC      := gcc
# The visualizer is a verification aid, not harness/solver code. We keep -Wall
# -Wextra for signal but not -Werror: the vendored stb_easy_font.h trips
# -Wmissing-braces / -Wunused-function, and snprintf truncation here is bounded
# and safe.
CFLAGS  := -std=c11 -Wall -Wextra -O2 -Wno-unknown-pragmas
INC     := -I../src -I..
LDLIBS  := -lm
B       := ../build

$(B)/contact_sheet: contact_sheet.c ../src/pep.c ../src/pep_codec.c \
                    ../src/pep.h ../src/pep_codec.h ../stb_easy_font.h
	$(CC) $(CFLAGS) $(INC) contact_sheet.c ../src/pep.c ../src/pep_codec.c -o $@ $(LDLIBS)

.PHONY: shots clean
# Render sheets for the committed images. Assumes the reference harness has run
# (build/q/*.qbin, build/out/*.pep, performance-test/results.txt exist); the
# project Makefile's `make all input` + run-performance-test produce them.
# NOTE: run from the repo ROOT — the manifest's pep_path column is repo-root
# relative (e.g. `build/out/<name>.pep`), so contact_sheet must resolve paths
# from there, not from viz/.
shots: $(B)/contact_sheet
	cd .. && build/contact_sheet performance-test/results.txt images build/q viz/shots

clean:
	rm -f $(B)/contact_sheet
