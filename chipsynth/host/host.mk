# Host build of the chip engine against the real SDK headers.
# Usage: make SDK=/path/to/kit      (SDK must contain sdk/include/fm1.h)
SDK ?= ../..
CC ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -DFM1_HOST_TEST -I../app -I$(SDK)/sdk/include
SRC = ../app/chip_engine.c ../app/chip_presets.c

all: test_host demo_host

test_host: test_host.c $(SRC) ../app/chip_engine.h
	$(CC) $(CFLAGS) -o $@ test_host.c $(SRC) -lm

demo_host: demo_host.c $(SRC) ../app/chip_engine.h
	$(CC) $(CFLAGS) -o $@ demo_host.c $(SRC) -lm

check: test_host audit
	./test_host

# The audio path must not pull in floating point or libm.
audit:
	@! grep -nE '\b(float|double)\b|math\.h|exp2|powf|sinf|cosf' ../app/chip_engine.c ../app/chip_presets.c ../app/chip_engine.h \
		&& echo "PASS  audit: no float/double/libm in the engine sources"

clean:
	rm -f test_host demo_host demo.wav
