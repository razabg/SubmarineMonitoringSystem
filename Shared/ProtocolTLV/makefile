# Shared/ProtocolTLV/Makefile - build and test the TLV protocol on Linux.
#
# This only covers the Linux side: the unit tests (test.c) and a
# check that the header links cleanly from C++ (link_test.cpp), for
# the Central Computer and Ground Station.
#
# It does NOT build the firmware. STM32CubeIDE compiles Tlv.c on its
# own once this folder is linked into the CubeIDE project - nothing
# to do here for that. This file exists so a change to Tlv.c can be
# tested in seconds on this machine, before ever opening CubeIDE.

CC      ?= gcc
CXX     ?= g++

# -Werror because a warning in protocol code is a bug waiting to
# happen on the other side of the wire.
# -pedantic keeps us on standard C99, which both compilers agree on.
CFLAGS   = -std=c99 -Wall -Wextra -Werror -pedantic -O2 -g \
           -Wconversion -Wshadow -Wstrict-prototypes
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -O2 -g

BUILD = build

.PHONY: all test cpp clean

all: test cpp

# ---------------------------------------------------------------
$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test: $(BUILD) tlv.c tlv_names.c test.c
	$(CC) $(CFLAGS) tlv.c tlv_names.c test.c -o $(BUILD)/test

test: $(BUILD)/test
	./$(BUILD)/test

# ---------------------------------------------------------------
# tlv.c is compiled by the C compiler, the check by the C++ one,
# then linked together - exactly how the real Central Computer and
# Ground Station builds work, since they are C++ programs linking
# against this plain-C protocol.
$(BUILD)/link_test: $(BUILD) tlv.c tlv_names.c link_test.cpp
	$(CC)  $(CFLAGS)   -c tlv.c         -o $(BUILD)/tlv.o
	$(CC)  $(CFLAGS)   -c tlv_names.c   -o $(BUILD)/tlv_names.o
	$(CXX) $(CXXFLAGS) -c link_test.cpp -o $(BUILD)/link_test.o
	$(CXX) $(BUILD)/tlv.o $(BUILD)/tlv_names.o $(BUILD)/link_test.o \
	      -o $(BUILD)/link_test

cpp: $(BUILD)/link_test
	./$(BUILD)/link_test

clean:
	rm -rf $(BUILD) a.out