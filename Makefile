###########################################
# Simple Makefile for PIC test program
#
# George Sun
# A-Concept Inc.
# 2019-03-15
###########################################

all: pictest libs

libs: libhidapi-hidraw.so

CC       ?= gcc
CFLAGS   ?= -Wall -g -fpic

CXX      ?= g++
CXXFLAGS ?= -Wall -g -fpic

LDFLAGS  ?= -Wall -g


COBJS     = hid.o
CPPOBJS   = pictest.o
OBJS      = $(COBJS) $(CPPOBJS)
LIBS_UDEV = `pkg-config libudev --libs` -lrt
LIBS      = $(LIBS_UDEV)
INCLUDES ?= -I../hidapi `pkg-config libusb-1.0 --cflags`


# Console Test Program
pictest: $(COBJS) $(CPPOBJS)
	$(CXX) $(LDFLAGS) $^ $(LIBS_UDEV) -o $@

# Shared Libs
libhidapi-hidraw.so: $(COBJS)
	$(CC) $(LDFLAGS) $(LIBS_UDEV) -shared -fpic -Wl,-soname,$@.0 $^ -o $@

# Objects
$(COBJS): %.o: %.c
	$(CC) $(CFLAGS) -c $(INCLUDES) $< -o $@

$(CPPOBJS): %.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $(INCLUDES) $< -o $@


clean:
	rm -f $(OBJS) pictest libhidapi-hidraw.so pictest.o

.PHONY: clean libs
