#pragma once

#include <bits/types/struct_timeval.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string>
#include <vector>

class test_state {
  bool m_passed{true};
public:
  inline void update(bool condition) noexcept {
    m_passed &= condition;
  }
  [[nodiscard]] inline bool passed() const noexcept {
    return m_passed;
  }
  [[nodiscard]] inline bool failed() const noexcept {
    return not m_passed;
  }
};

class framework {
  int total_points{0};
  int earned_points{0};
  int total_tests{0};
  int failed_tests{0};
  int passed_tests{0};

  std::vector<std::string> description_stack;
  bool root{true};
  bool passed{true};
  bool verbose{false};
  int time_limit = 1000;

  int snd_fd{-1};
  int rcv_fd{-1};

  inline std::string make_message_stack() const noexcept {
    std::string msg;
    for (auto s : description_stack) {
      msg += s;
      msg += '\n';
    }
    return msg;
  }

  inline void send(std::string const& msg) noexcept {
    int sz = static_cast<int>(msg.size());
    write(snd_fd, &sz, sizeof(sz));
    write(snd_fd, msg.data(), sz);
  }

  inline std::optional<std::string> receive() noexcept {
    int sz = 0;
    long bytes_read = read(rcv_fd, &sz, sizeof(sz));
    if (bytes_read <= 0) {
      return {};
    } else {
      std::string str;
      str.resize(sz);
      read(rcv_fd, str.data(), sz);
      return str;
    }
  }

public:

  inline framework(bool verbose) : verbose{verbose} {}
  inline framework(int time_limit) : time_limit{time_limit} {}
  inline framework(bool verbose, int time_limit) : verbose{verbose}, time_limit{time_limit} {}

  framework() = default;
  framework(framework const&) = delete;
  framework(framework &&) = delete;
  framework& operator=(framework const&) = delete;
  framework& operator=(framework &&) = delete;

  inline void
  require(std::string const& description, bool condition) noexcept {
    bool should_send = not condition || verbose;
    if (should_send) {
      std::string buffer = make_message_stack();
      buffer += (condition ? "PASS: " : "FAIL: ");
      buffer += description + '\n';
      send(buffer);
    }
  }

  template <typename T>
  inline void
  equal(std::string const& description, T const& lhs, T const& rhs) noexcept {
    require(description, lhs == rhs);
  }

  template <typename Callable>
  inline void
  scenario(std::string const& description, Callable&& nesting) noexcept {
    operator()("Scenario: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable>
  inline void
  given(std::string const & description, Callable&& nesting) noexcept {
    operator()("Given: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable>
  inline void
  when(std::string const & description, Callable&& nesting) noexcept {
    operator()("When: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable>
  inline void
  then(std::string const & description, Callable&& nesting) noexcept {
    operator()("Then: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable>
  inline void
  scenario(int points, std::string const& description, Callable&& nesting) noexcept {
    operator()(points, "Scenario: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable>
  inline void
  given(int points, std::string const & description, Callable&& nesting) noexcept {
    operator()(points, "Given: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable>
  inline void
  when(int points, std::string const & description, Callable&& nesting) noexcept {
    operator()(points, "When: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable>
  inline void
  then(int points, std::string const & description, Callable&& nesting) noexcept {
    operator()(points, "Then: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable>
  inline void
  operator()(int points, std::string description, Callable&& nesting) noexcept {
    description_stack.push_back(description);
    if (points != 0) {
      ++total_tests;
      total_points += points;
      passed = true;
    }

    int pipe_channels[2];
    pipe(pipe_channels);
    pid_t pid = fork();
    if (pid < 0) {
      send("error forking process");
      exit(-1);
    } else if (pid == 0) {
      // child
      root = false;
      close(pipe_channels[STDIN_FILENO]);
      snd_fd = pipe_channels[STDOUT_FILENO];
      nesting();
      close(snd_fd);
      exit(0);
    } else {
      // parent
      setpgid(pid, getpid());
      close(pipe_channels[STDOUT_FILENO]);
      rcv_fd = pipe_channels[STDIN_FILENO];
      struct timeval start;
      gettimeofday(&start, NULL);
      int ret_code;
      std::optional<std::string> additional_msg = std::nullopt;
      while (true) {

        // child is dead, inspect ret_code
        if (int res = waitpid(pid, &ret_code, WNOHANG); res != 0) {
          if (ret_code != 0) {
            additional_msg = "Process exited with abnormal status code: " + std::to_string(ret_code);
          }
          break;
        }

        // check timeout
        {
          struct timeval curr;
          gettimeofday(&curr, NULL);
          int time_in_ms = static_cast<int>(curr.tv_sec - start.tv_sec) * 1000 + static_cast<int>(curr.tv_usec - start.tv_usec) / 1000;
          if (time_in_ms > time_limit) {
            kill(-pid, SIGKILL);
            waitpid(pid, NULL, 0);
            additional_msg = "Process exceeded time limit of " + std::to_string(time_limit) + "ms";
            break;
          }
        }

        // collect all children messages and propagate upward
        while (true) {
          if (auto msg = receive(); msg) {
            if (root) {
              std::cout << *msg << '\n';
            } else {
              send(*msg);
            }
          }
        }
        if (additional_msg) {
          if (root) {
            std::cout << *additional_msg << '\n';
          } else {
            send(*additional_msg);
          }
        }
      }
    }

    if (points != 0) {
      if (passed) {
        earned_points += points;
        ++passed_tests;
      } else {
        --failed_tests;
      }
    }
    description_stack.pop_back();
  }

  template <typename Callable>
  inline void
  operator()(std::string description, Callable&& nesting) noexcept {
    (*this)(0, description, std::forward<Callable>(nesting));
  }
};
