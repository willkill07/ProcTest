CXXFLAGS := -Os -std=c++17 -Wall -Wextra -Wpedantic -Wconversion

.PHONY: all clean

LINK.o := $(CXX) $(CXXFLAGS)

all : example

example : example.o proc_test.o

proc_test.o : proc_test.hpp
example.o : proc_test.hpp

clean :
	-rm -rf example example.o
