#include "proc_test.hpp"

#include <algorithm>
#include <vector>

int main() {
  framework grader;
  grader.scenario("The testing framework functions as expected", [&] {
    grader.given("A scenario", [&] {
      grader.when(16, "Points are defined on the 'when' clause and we have a true assertion", [&] {
        grader.then("we earn points", [&] {
          grader.require("this is true", true);
        });
      });
      grader.when(8, "Points are defined on the 'then' clause and multiple assertions where one is false", [&] {
        grader.then("we do not earn points", [&] {
          grader.require("this first assertion is true", true);
          grader.require("this second assertion is false", false);
        });
      });
      grader.when(4, "Points are defined on the 'then' clause and multiple assertions where one is false", [&] {
        grader.then("we do not earn points", [&] {
          grader.require("this first assertion is false", false);
          grader.require("this second assertion is true", true);
        });
      });

      grader.when(2, "We test an invalid memory access", [&] {
        std::vector<int> x;
        x[10293] = 0;
        grader.then("we do not earn points and we detect it!", [&] {
          grader.require("this is true", true);
        });
      });

      grader.when(1, "We test an infinite loop", [&] {
        std::vector<int> x;
        x.resize(5);
        while (!x.empty()) {
          x.erase(std::remove(x.begin(), x.end(), 5), x.end());
        }
        grader.then("we do not earn points and we detect it!", [&] {
          grader.require("this is true", true);
        });
      });
    });
  });
  return grader.status();
}
