CC      = gcc
CFLAGS  = -Wall -g -I../include  # Add the path to your include directory here
AR      = ar
ARFLAGS = rcs
SOURCES = lwp.c
OBJECTS = $(SOURCES:.c=.o)
ASSEMBLY = magic64.o  # Add the name of the assembly object file here
TARGET  = liblwp.a

all: $(TARGET)

$(TARGET): $(OBJECTS) $(ASSEMBLY)
	$(AR) $(ARFLAGS) $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)


