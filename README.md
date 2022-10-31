# ProcTest

`proc_test` is a small unit testing framework designed to unit test problematic
code which may exhibit failures during runtime, namely:
- segmentation faults
- dereferencing null
- infinite loops

Its primary use may be creating unit-test autograders for a course in data structures.

This is now functioning!

## Integrating

* Ensure that `#include "proc_test.hpp"` is in your entrypoint driver

```cpp
#include "proc_test.hpp"

int main() {
  // true is for verbose
  framework f{true};
  f("A Simple Test", [&] {
    std::vector<int> x;
    f.require("size must be zero", x.size() == 0);
  });
  f(5, "A points-valued test", [&] {
    std::vector<int> s;
    s.push_back(4);
    f.require("value pushed back must be at back", s.back() == 4);
  });
  f("A segmentation fault", [&] {
    std::vector<int> x;
    x[10293] = 0;
    f.require("Should never get here", true);
  });
  f("An infinite loop", [&] {
    std::vector<int> x;
    while (x.size() != 2) {
      x.push_back(2);
      x.push_back(3);
      x.clear();
    }
    f.require("Should never get here", true);
  });
}
```

## Building

1. compile `proc_test.cpp`
2. link `proc_test.o` with your entrypoint driver

## Running

* Simply invoke your entrypoint driver