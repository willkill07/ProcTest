#pragma once

#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <functional>
#include <iostream>
#include <optional>
#include <ratio>
#include <sstream>
#include <string>
#include <thread>
#include <variant>
#include <vector>

using std::string_literals::operator""s;
using std::literals::       operator""ms;

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
  int total_assertions{0};
  int passed_assertions{0};

  [[gnu::noinline]] test_metrics& operator+=(test_metrics const& other) noexcept {
    total_points += other.total_points;
    earned_points += other.earned_points;
    total_tests += other.total_tests;
    failed_tests += other.failed_tests;
    passed_tests += other.passed_tests;
    total_assertions += other.total_assertions;
    passed_assertions += other.passed_assertions;
    return *this;
  }

  [[gnu::noinline]] friend std::ostream& operator<<(std::ostream& os, test_metrics const& m) noexcept {
    os << "IMPORTANT NOTE: reports below do not necessarily mean all tests ran. See any error messages above!" << '\n'
       << "Tests: " << m.passed_tests << '/' << m.total_tests << " [Failed " << m.failed_tests << " test(s)]" << '\n'
       << "Points: " << m.earned_points << '/' << m.total_points << '\n'
       << "Assertions: " << m.passed_assertions << '/' << m.total_assertions << '\n';
    return os;
  }

  [[gnu::noinline]] void reset() noexcept {
    total_points      = 0;
    earned_points     = 0;
    total_tests       = 0;
    failed_tests      = 0;
    passed_tests      = 0;
    total_assertions  = 0;
    passed_assertions = 0;
  }
};

namespace serialization {

namespace test_metrics {
[[gnu::noinline]] static void write(::test_metrics const& m, int fd) noexcept {
  ::write(fd, &m, sizeof(::test_metrics));
}
[[gnu::noinline]] static ::test_metrics read(int fd) noexcept {
  ::test_metrics m;
  ::read(fd, &m, sizeof(::test_metrics));
  return m;
}
} // namespace test_metrics

namespace string {
[[gnu::noinline]] static void write(std::string const& str, int fd) noexcept {
  int sz = static_cast<int>(str.size());
  ::write(fd, &sz, sizeof(sz));
  ::write(fd, str.data(), sz);
}
[[gnu::noinline]] static std::string read(int fd) noexcept {
  int sz;
  ::read(fd, &sz, sizeof(sz));
  std::string res;
  res.resize(sz);
  ::read(fd, std::addressof(res[0]), sz);
  return res;
}
} // namespace string

namespace boolean {
[[gnu::noinline]] static void write(bool const& b, int fd) noexcept {
  ::write(fd, &b, sizeof(b));
}
[[gnu::noinline]] static bool read(int fd) noexcept {
  bool b;
  ::read(fd, &b, sizeof(b));
  return b;
}
} // namespace boolean

} // namespace serialization

enum class packet_header {
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
template <> struct type_for<packet_header::string> {
  using type = serializable_string;
};
template <> struct type_for<packet_header::test_metrics> {
  using type = serializable_test_metrics;
};
} // namespace detail
template <packet_header PH> using type_for = typename detail::type_for<PH>::type;

template <typename... OverloadSet> struct Overload : OverloadSet... {
  using OverloadSet::operator()...;
};
template <class... OverloadSet> Overload(OverloadSet...) -> Overload<OverloadSet...>;

class framework {

  std::vector<std::string> description_stack;
  int                      level{0};
  bool                     verbose{false};
  int                      time_limit{1000};
  std::optional<bool>      passed{std::nullopt};
  int                      snd_fd{-1};
  int                      rcv_fd{-1};
  test_metrics             metrics;
  bool                     points_specified{false};

  [[gnu::noinline]] std::string make_message_stack() const noexcept {
    std::string msg;
    for (auto const& s : description_stack) {
      msg += s;
      msg += '\n';
    }
    return msg;
  }

  [[gnu::noinline]] void send(bool const& flag) const noexcept {
    packet_header header{packet_header::boolean};
    write(snd_fd, &header, sizeof(packet_header));
    serializable_bool{flag}.write(snd_fd);
  }

  [[gnu::noinline]] void send(std::string const& msg) const noexcept {
    packet_header header{packet_header::string};
    write(snd_fd, &header, sizeof(packet_header));
    serializable_string{msg}.write(snd_fd);
  }

  [[gnu::noinline]] void send(test_metrics const& met) const noexcept {
    packet_header header{packet_header::test_metrics};
    write(snd_fd, &header, sizeof(packet_header));
    serializable_test_metrics{met}.write(snd_fd);
  }

  [[gnu::noinline]] serializable_types receive() const noexcept {
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

  [[gnu::noinline]] void child(int points, std::function<void()> nesting) noexcept {
    // child
    ++level;

    points_specified = points_specified || (points != 0);
    passed = std::nullopt;
    metrics.reset();
    if (points_specified and points != 0) {
      ++metrics.total_tests;
      metrics.total_points += points;
    }

    nesting();

    if (points_specified) {
      if (points == 0) {
        send(passed.value_or(false));
      } else {
        if (passed.value_or(false)) {
          metrics.earned_points += points;
          ++metrics.passed_tests;
        } else {
          ++metrics.failed_tests;
        }
      }
    }
    send(metrics);

    close(snd_fd);
    exit(0);
  }

  [[gnu::noinline]] void parent(int pid) noexcept {
    std::optional<std::string> additional_msg{std::nullopt};

    auto const start = std::chrono::steady_clock::now();

    do {
      int status;
      int res = waitpid(pid, &status, WNOHANG);
      if (res == pid) {
        if (WIFEXITED(status)) {
          break;
        }
        if (WIFSIGNALED(status)) {
          std::string msg = "The following test failed to run! Status code: "s;
          msg += std::to_string(WTERMSIG(status));
          msg += " ("s + strsignal(WTERMSIG(status)) + ')';
          msg += '\n';
          msg += make_message_stack();
          msg += '\n';
          additional_msg = msg;
          break;
        }
      } else if (res == 0 and level > 0 and points_specified) {
        auto const stop     = std::chrono::steady_clock::now();
        auto const duration = (stop - start);
        if ((stop - start) > std::chrono::duration<int, std::milli>(time_limit)) {
          kill(-pid, SIGKILL);
          std::string msg = "The following test exceeded the time limit of "s;
          msg += std::to_string(time_limit) + "ms";
          msg += '\n';
          msg += make_message_stack();
          msg += '\n';
          additional_msg = msg;
          break;
        }
      }
      std::this_thread::sleep_for(0.5ms);
    } while (true);

    // collect all children messages and propagate upward
    auto overload_set = Overload{[](std::monostate) noexcept {
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
                                   if (passed.has_value()) {
                                     passed = passed && child_passed;
                                   } else {
                                     passed = child_passed;
                                   }
                                   return true;
                                 }};
    while (std::visit(overload_set, receive())) {
      ;
    }
    if (additional_msg) {
      if (level == 0) {
        (std::cout << *additional_msg).flush();
      } else {
        send(*additional_msg);
      }
    }
    description_stack.pop_back();
  }

public:
  framework(bool verbose) : verbose{verbose} {
  }
  framework(int time_limit) : time_limit{time_limit} {
  }
  framework(bool verbose, int time_limit) : verbose{verbose}, time_limit{time_limit} {
  }

  framework()                            = default;
  framework(framework const&)            = delete;
  framework(framework&&)                 = delete;
  framework& operator=(framework const&) = delete;
  framework& operator=(framework&&)      = delete;

  ~framework() {
    if (level == 0) {
      std::cout << metrics << std::endl;
    }
  }

  [[gnu::noinline]] void require(std::string const& description, bool condition) noexcept {
    ++metrics.total_assertions;
    if (not condition) {
      passed = false;
    } else {
      ++metrics.passed_assertions;
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

  template <typename Callable> void when(int points, std::string const& description, Callable&& nesting) noexcept {
    operator()(points, "When: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable> void then(int points, std::string const& description, Callable&& nesting) noexcept {
    operator()(points, "Then: " + description, std::forward<Callable>(nesting));
  }

  template <typename Callable> void operator()(int points, std::string const& description, Callable&& nesting) noexcept {
    description_stack.push_back(description);
    int pipe_channels[2];
    pipe(pipe_channels);
    pid_t pid = fork();
    if (pid < 0) {
      send("error forking process");
      exit(-1);
    } else if (pid == 0) {
      close(pipe_channels[STDIN_FILENO]);
      snd_fd = pipe_channels[STDOUT_FILENO];
      child(points, nesting);
    } else {
      setpgid(pid, getpid());
      close(pipe_channels[STDOUT_FILENO]);
      rcv_fd = pipe_channels[STDIN_FILENO];
      parent(pid);
    }
  }

  template <typename Callable> void operator()(std::string const& description, Callable&& nesting) noexcept {
    (*this)(0, description, std::forward<Callable>(nesting));
  }
};
