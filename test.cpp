#include "proc_test.hpp"

int main() {
  framework grader{true};

  grader.scenario("Outer", [&] {
    grader.given("Inner 1", [&] {
      grader.when(10, "Part 1", [&] {
        grader.then("A", [&] {
          grader.require("this is true", true);
          grader.require("this is false", false);
        });
        grader.then("B", [&] {
          grader.require("this is true", true);
          grader.require("this is false", false);
        });
        grader.then("C", [&] {
          grader.require("this is true", true);
          grader.require("this is false", false);
        });
      });
      grader.when(10, "Part 2", [&] {
        grader.then("A", [&] {
          grader.require("this is true", true);
          grader.require("this is false", false);
        });
        grader.then("B", [&] {
          grader.require("this is true", true);
          grader.require("this is false", false);
        });
        grader.then("C", [&] {
          grader.require("this is true", true);
          grader.require("this is false", false);
        });
      });
    });
    grader.given("Inner 2", [&] {
      grader.when(10, "Part 1", [&] {
        grader.then("A", [&] {
          grader.require("this is true", true);
          grader.require("this is false", false);
        });
        grader.then("B", [&] {
          grader.require("this is true", true);
          grader.require("this is false", false);
        });
        grader.then("C", [&] {
          grader.require("this is true", true);
          grader.require("this is false", false);
        });
      });
      grader.when(10, "Part 2", [&] {
        grader.then("A", [&] {
          grader.require("this is true", true);
          grader.require("this is false", false);
        });
        grader.then("B", [&] {
          grader.require("this is true", true);
          grader.require("this is false", false);
        });
        grader.then("C", [&] {
          grader.require("this is true", true);
          grader.require("this is false", false);
        });
      });
    });
  });
}