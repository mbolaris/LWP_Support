CC      = gcc
CFLAGS  = -Wall -g -I../include 
LD      = gcc
LDFLAGS = -Wall -g -L. -Wl,-rpath=.
LIBS    = -llwp  # Always link against liblwp.a
SNAKELIBS = $(LIBS) -lsnakes -lncurses -lrt  # Additional libs for snakes

AS      = gcc  # Use GCC for assembling to leverage the -m64 option
ASFLAGS = -c -m64  # Correct option for 64-bit
LWPSRC  = lwp.c
ASMSRC  = magic64.S
LWPOBJ  = $(LWPSRC:.c=.o)
ASMOBJ  = $(ASMSRC:.S=.o)
OBJECTS = $(LWPOBJ) $(ASMOBJ)
TARGET  = liblwp.a
PROGS   = snakes nums hungry
SNAKEOBJS = randomsnakes.o util.o
HUNGRYOBJS = hungrysnakes.o util.o
NUMOBJS = numbersmain.o

.PHONY: all clean nums ns rs hs

all:    $(TARGET) $(PROGS)

$(TARGET): $(OBJECTS)
	ar rcs $@ $^

nums: $(NUMOBJS) $(TARGET)
	$(LD) $(LDFLAGS) -o $@ $(NUMOBJS) $(LIBS)

snakes: $(SNAKEOBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(SNAKELIBS)

hungry: $(HUNGRYOBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(SNAKELIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(AS) $(ASFLAGS) $< -o $@

clean:
	rm -f *.o *~ TAGS $(PROGS) $(TARGET)


ns: nums
	./nums

rs: snakes
	./snakes

hs: hungry
	./hungry
