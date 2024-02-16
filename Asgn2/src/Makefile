CC      = gcc
CFLAGS  = -Wall -g -I../include -O0
LD      = gcc
LDFLAGS = -Wall -g -L.
LIBS    = -llwp  # Always link against liblwp.a
AS      = gcc  # Use GCC for assembling to leverage the -m64 option
ASFLAGS = -c -m64  # Correct option for 64-bit
LWPSRC  = lwp.c  # C source files
ASMSRC  = magic64.S  # Assembly source files
LWPOBJ  = $(LWPSRC:.c=.o)  # Convert .c to .o
ASMOBJ  = $(ASMSRC:.S=.o)  # Convert .S to .o
OBJECTS = $(LWPOBJ) $(ASMOBJ)  # All object files
TARGET  = liblwp.a
PROGS   = nums
NUMOBJS = numbersmain.o

.PHONY: all clean nums ns

all:    $(TARGET) $(PROGS)

$(TARGET): $(OBJECTS)
	ar rcs $@ $^

nums: $(NUMOBJS) $(TARGET)
	$(LD) $(LDFLAGS) -o $@ $(NUMOBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(AS) $(ASFLAGS) $< -o $@

clean:
	rm -f $(OBJECTS) *~ TAGS $(PROGS) $(TARGET)

ns: nums
	./nums
