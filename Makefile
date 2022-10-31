CXXFLAGS := -Os -g -std=c++17 -Wall -Wextra -Wpedantic -Wconversion

.PHONY: all clean

LINK.o := $(CXX) $(CXXFLAGS)

all : test

test.o : proc_test.hpp

clean :
	-rm -rf test test.o
