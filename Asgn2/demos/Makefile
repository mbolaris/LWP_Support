CC      = gcc
CFLAGS  = -Wall -g -I../include
LD      = gcc

# Conditional library paths and linking
ifeq ($(USE_LWP_LIB),yes)
    LDFLAGS = -Wall -g -L../src -L../lib64  # Add both paths
    LIBS = -llwp
else
    LDFLAGS = -Wall -g -L../lib64
    LIBS = -lPLN
endif
SNAKELIBS = $(LIBS) -lsnakes -lncurses -lrt  # Use LIBS variable for conditional linking

PROGS   = snakes nums hungry
SNAKEOBJS = randomsnakes.o util.o
HUNGRYOBJS = hungrysnakes.o util.o
NUMOBJS = numbersmain.o
OBJS    = $(SNAKEOBJS) $(HUNGRYOBJS) $(NUMOBJS)
EXTRACLEAN = core $(PROGS)

.PHONY: all allclean clean rs hs ns

all:    $(PROGS)

allclean: clean
	@rm -f $(EXTRACLEAN)

clean:
	rm -f $(OBJS) *~ TAGS

snakes: $(SNAKEOBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(SNAKELIBS)

hungry: $(HUNGRYOBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(SNAKELIBS)

nums: $(NUMOBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

rs: snakes
	export LD_LIBRARY_PATH=../lib64; ./snakes

hs: hungry
	export LD_LIBRARY_PATH=../lib64; ./hungry

ns: nums
	export LD_LIBRARY_PATH=../lib64; ./nums

