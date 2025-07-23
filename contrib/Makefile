CFLAGS += -std=gnu11 -D_GNU_SOURCE -frecord-gcc-switches -fdiagnostics-color=always -save-temps=obj -fverbose-asm -Wall -Werror
DBGFLAGS += -Og -fno-omit-frame-pointer -g3 -gdwarf-5 -fsanitize=address,undefined -Wl,--export-dynamic
RELFLAGS += -O2 -DNDEBUG -march=native -ftree-vectorize -fvect-cost-model=very-cheap -flto -ffat-lto-objects -Wl,--strip-all
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
	rm -f _debug _release *.i *.o *.s *.ltrans* *.res *.args* $(PROGS)
distclean:
	git clean -xdf
