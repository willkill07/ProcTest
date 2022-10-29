#include "proc_test.hpp"

int main() {
  framework grader{true};
  grader.scenario("Outer 1", [&] {
    grader.given("Inner 1", [&] {
      grader.when("Part 2", [&] {
        grader.then(10, "A", [&] {
          int* x;
          std::cout << *x << std::endl;
          grader.require("this is false", false);
        });
        grader.then(10, "B", [&] {
          while (true) {}
          grader.require("this is false", false);
        });
      });
    });
  });
}