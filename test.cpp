#include "proc_test.hpp"

#include <algorithm>
#include <vector>

int main() {
  framework grader;
  grader.scenario("Outer 1", [&] {
    grader.given("Inner 1", [&] {
      grader.when(15, "Part 1", [&] {
        grader.then("A", [&] {
          grader.require("this is true", true);
        });
        grader.then("B", [&] {
          grader.require("this is true", true);
        });
      });
    });
    grader.when(5, "Part 2", [&] {
      grader.then("A", [&] {
        std::vector<int> x;
        x.resize(5);
        while (!x.empty()) {
          x.erase(std::remove(x.begin(), x.end(), 5), x.end());
        }
        grader.require("this is true", true);
      });
      grader.then("B", [&] {
        grader.require("this is true", true);
      });
    });
    grader.when("Part 3", [&] {
      grader.then(5, "A", [&] {
        std::vector<int> x;
        x[10293] = 0;
        grader.require("this is true", false);
      });
      grader.then(5, "B", [&] {
        grader.require("this is true", true);
      });
    });
  });
}