#pragma once

#include <poll.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

template <typename T, T (*Reader)(int), void (*Writer)(T const&, int)> class FDSerializable {
  T data;

public:
  FDSerializable() = delete;
  FDSerializable(T const& data) : data{data} {
  }

  operator T&() {
    return data;
  }
  operator T const&() const {
    return data;
  }

  T* operator->() noexcept {
    return &data;
  }

  T const* operator->() const noexcept {
    return &data;
  }

  void write(int fd) const noexcept {
    Writer(*this, fd);
  }
  static T read_from(int fd) noexcept {
    return Reader(fd);
  }
};

struct test_metrics {
  int total_points{0};
  int earned_points{0};
  int total_tests{0};
  int failed_tests{0};
  int passed_tests{0};

  test_metrics& operator+=(test_metrics const& other) noexcept {
    total_points += other.total_points;
    earned_points += other.earned_points;
    total_tests += other.total_tests;
    failed_tests += other.failed_tests;
    passed_tests += other.passed_tests;
    return *this;
  }

  friend std::ostream& operator<<(std::ostream& os, test_metrics const& m) noexcept {
    os << "Tests: " << m.passed_tests << '/' << m.total_tests << " (Failed " << m.failed_tests << " tests)" << '\n'
       << "Points: " << m.earned_points << '/' << m.total_points << '\n';
    return os;
  }

  void reset() noexcept {
    total_points  = 0;
    earned_points = 0;
    total_tests   = 0;
    failed_tests  = 0;
    passed_tests  = 0;
  }
};

namespace serialization {
namespace test_metrics {
static void write(::test_metrics const& m, int fd) noexcept {
  ::write(fd, &m, sizeof(::test_metrics));
}
static ::test_metrics read(int fd) noexcept {
  ::test_metrics m;
  ::read(fd, &m, sizeof(::test_metrics));
  return m;
}
} // namespace test_metrics
namespace string {
static void write(std::string const& str, int fd) noexcept {
  int sz = static_cast<int>(str.size());
  ::write(fd, &sz, sizeof(sz));
  ::write(fd, str.data(), sz);
}
static std::string read(int fd) noexcept {
  int sz;
  ::read(fd, &sz, sizeof(sz));
  std::string res;
  res.resize(sz);
  ::read(fd, res.data(), sz);
  return res;
}
} // namespace string
namespace boolean {
static void write(bool const& b, int fd) noexcept {
  ::write(fd, &b, sizeof(b));
}
static bool read(int fd) noexcept {
  bool b;
  ::read(fd, &b, sizeof(b));
  return b;
}
} // namespace boolean
} // namespace serialization

enum class packet_header
{
  string,
  test_metrics,
  boolean
};

using serializable_string       = FDSerializable<std::string, serialization::string::read, serialization::string::write>;
using serializable_test_metrics = FDSerializable<test_metrics, serialization::test_metrics::read, serialization::test_metrics::write>;
using serializable_bool         = FDSerializable<bool, serialization::boolean::read, serialization::boolean::write>;
using serializable_types        = std::variant<std::monostate, serializable_string, serializable_test_metrics, serializable_bool>;

namespace detail {
template <packet_header> struct type_for;
template <> struct type_for<packet_header::string> { using type = serializable_string; };
template <> struct type_for<packet_header::test_metrics> { using type = serializable_test_metrics; };
} // namespace detail
template <packet_header PH> using type_for = typename detail::type_for<PH>::type;

template <typename... OverloadSet> struct Overload : OverloadSet... { using OverloadSet::operator()...; };
template <class... OverloadSet> Overload(OverloadSet...) -> Overload<OverloadSet...>;

class framework {

  std::vector<std::string> description_stack;
  test_metrics             metrics;
  int                      level{0};
  bool                     verbose{false};
  int                      time_limit{1000};

  bool passed{true};
  bool points_specified{false};

  int snd_fd{-1};
  int rcv_fd{-1};

  std::string make_message_stack() const noexcept {
    std::string msg;
    for (auto const& s : description_stack) {
      msg += s;
      msg += '\n';
    }
    return msg;
  }

  void send(bool const& flag) const noexcept {
    packet_header header{packet_header::boolean};
    write(snd_fd, &header, sizeof(packet_header));
    serializable_bool{flag}.write(snd_fd);
  }

  void send(std::string const& msg) const noexcept {
    packet_header header{packet_header::string};
    write(snd_fd, &header, sizeof(packet_header));
    serializable_string{msg}.write(snd_fd);
  }

  void send(test_metrics const& met) const noexcept {
    packet_header header{packet_header::test_metrics};
    write(snd_fd, &header, sizeof(packet_header));
    serializable_test_metrics{met}.write(snd_fd);
  }

  serializable_types receive() const noexcept {
    pollfd fds;
    fds.fd     = rcv_fd;
    fds.events = POLLIN;
    if (poll(&fds, 1, 10) < 0) {
      puts("Error in poll");
      exit(-1);
    }
    if ((fds.revents & POLLIN) == 0) {
      return {};
    }
    packet_header header;
    if (read(rcv_fd, &header, sizeof(header)) < 0) {
      puts("Error in read");
      exit(-1);
    }
    switch (header) {
    case packet_header::string:
      return serializable_string::read_from(rcv_fd);
    case packet_header::test_metrics:
      return serializable_test_metrics::read_from(rcv_fd);
    case packet_header::boolean:
      return serializable_bool::read_from(rcv_fd);
    }
    return {};
  }

public:
  framework(bool verbose) : verbose{verbose} {
  }
  framework(int time_limit) : time_limit{time_limit} {
  }
  framework(bool verbose, int time_limit) : verbose{verbose}, time_limit{time_limit} {
  }

  framework()                 = default;
  framework(framework const&) = delete;
  framework(framework&&)      = delete;
  framework& operator=(framework const&) = delete;
  framework& operator=(framework&&) = delete;

  void require(std::string const& description, bool condition) noexcept {
    if (not condition) {
      passed = false;
    }
    if (not condition || verbose) {
      send(make_message_stack() + (condition ? "PASS: " : "FAIL: ") + description + '\n' + '\n');
    }
  }

  template <typename T> void equal(std::string const& description, T const& lhs, T const& rhs) noexcept {
    require(description, lhs == rhs);
  }

  template <typename Callable> void scenario(std::string const& description, Callable&& nesting) noexcept {
    operator()("Scenario: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable> void given(std::string const& description, Callable&& nesting) noexcept {
    operator()("Given: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable> void when(std::string const& description, Callable&& nesting) noexcept {
    operator()("When: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable> void then(std::string const& description, Callable&& nesting) noexcept {
    operator()("Then: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable> void scenario(int points, std::string const& description, Callable&& nesting) noexcept {
    operator()(points, "Scenario: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable> void given(int points, std::string const& description, Callable&& nesting) noexcept {
    operator()(points, "Given: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable> void when(int points, std::string const& description, Callable&& nesting) noexcept {
    operator()(points, "When: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable> void then(int points, std::string const& description, Callable&& nesting) noexcept {
    operator()(points, "Then: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable> void operator()(int points, std::string description, Callable&& nesting) noexcept {
    description_stack.push_back(description);
    int pipe_channels[2];
    pipe(pipe_channels);
    pid_t pid = fork();
    if (pid < 0) {
      send("error forking process");
      exit(-1);
    } else if (pid == 0) {
      // child
      ++level;
      close(pipe_channels[STDIN_FILENO]);
      snd_fd = pipe_channels[STDOUT_FILENO];
      points_specified = (points != 0);
      test_metrics m;
      if (points_specified) {
        ++m.total_tests;
        m.total_points += points;
      }

      nesting();

      if (points_specified) {
        if (points == 0) {
          send(passed);
        } else {
          if (passed) {
            m.earned_points += points;
            ++m.passed_tests;
          } else {
            ++m.failed_tests;
          }
          send(m);
        }
      }
      
      close(snd_fd);
      exit(0);
    }

    // parent
    setpgid(pid, getpid());
    close(pipe_channels[STDOUT_FILENO]);
    rcv_fd = pipe_channels[STDIN_FILENO];
    struct timeval start;
    gettimeofday(&start, NULL);

    std::optional<std::string> additional_msg{std::nullopt};
    while (true) {

      // child is dead, inspect ret_code
      int ret_code;
      if (int res = waitpid(pid, &ret_code, WNOHANG); res != 0) {
        if (ret_code != 0) {
          additional_msg = "The following test failed to run! Status code: " + std::to_string(ret_code) + '\n';
        }
        break;
      }

      // check timeout
      if (points_specified) {
        struct timeval curr;
        gettimeofday(&curr, NULL);
        int time_in_ms = static_cast<int>(curr.tv_sec - start.tv_sec) * 1000 + static_cast<int>(curr.tv_usec - start.tv_usec) / 1000;
        if (time_in_ms > time_limit) {
          kill(-pid, SIGKILL);
          waitpid(pid, NULL, 0);
          additional_msg = "The following test exceeded the time limit of " + std::to_string(time_limit) + "ms !\n";
          break;
        }
      }
    }

    // collect all children messages and propagate upward
    while (true) {
      bool const res = std::visit(Overload{[](std::monostate) noexcept {
                                             return false;
                                           },
                                           [&](serializable_string const& message) noexcept {
                                             if (level == 0) {
                                               (std::cout << message->c_str()).flush();
                                             } else {
                                               send(message);
                                             }
                                             return true;
                                           },
                                           [&](serializable_test_metrics const& child_metrics) noexcept {
                                             metrics += child_metrics;
                                             return true;
                                           },
                                           [&](serializable_bool const& child_passed) noexcept {
                                             passed = passed && child_passed;
                                             return true;
                                           }},
                                  receive());
      if (not res) {
        break;
      }
    }
    if (additional_msg) {
      if (level == 0) {
        std::cout << *additional_msg;
        std::cout.flush();
      } else {
        send(*additional_msg);
      }
    }

    if (level > 0) {
      send(metrics);
    } else {
      std::cout << metrics << std::endl;
    }
    description_stack.pop_back();
  }

  template <typename Callable> void operator()(std::string description, Callable&& nesting) noexcept {
    (*this)(0, description, std::forward<Callable>(nesting));
  }
};
