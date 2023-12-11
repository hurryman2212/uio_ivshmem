CFLAGS += -frecord-gcc-switches --save-temps -fverbose-asm -Wall -Wextra -Werror -std=gnu11 -D_GNU_SOURCE
DBGFLAGS += -g3 -gdwarf-5 -Og -fno-omit-frame-pointer -fsanitize=address,undefined -Wl,-export-dynamic
RELFLAGS += -DNDEBUG -march=native -O2 -ftree-vectorize -fvect-cost-model=very-cheap -Wl,-s
LDFLAGS += 

SRCS := $(wildcard *.c)
PROGS := $(patsubst %.c,%,$(SRCS))

all: debug

debug: CFLAGS += $(DBGFLAGS)
debug: _debug build
_debug:
	@if test -f _release; then \
		make clean; \
	fi
	@touch _debug

release: CFLAGS += $(RELFLAGS)
release: _release build
_release:
	@if test -f _debug; then \
		make clean; \
	fi
	@touch _release

build: $(PROGS)
%: %.c
	$(CC) -o $@ $(CFLAGS) $< -Wl,--no-whole-archive $(LDFLAGS)

install: build
	cp $(PROGS) /usr/local/bin/
uninstall:
	rm -f $(PROGS:%=/usr/local/bin/%)

clean:
	rm -f _debug _release *.i *.o *.s $(PROGS)
distclean:
	git clean -xdf
