CXXFLAGS := -O2 -g -std=c++17 -Wall -Wextra -Wpedantic -Wconversion

.PHONY: all clean

all : test

clean :
	-rm -rf test test.o
