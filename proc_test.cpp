#include "proc_test.hpp"

#include <chrono>
#include <iostream>
#include <ratio>
#include <sstream>
#include <string>
#include <thread>

#include <cstdlib>
#include <cstring>

#include <sys/poll.h>
#include <sys/wait.h>

using std::string_literals::operator""s;
using std::literals::       operator""ms;

[[gnu::noinline]] test_metrics& test_metrics::operator+=(test_metrics const& other) noexcept {
  total_points += other.total_points;
  earned_points += other.earned_points;
  total_tests += other.total_tests;
  failed_tests += other.failed_tests;
  passed_tests += other.passed_tests;
  total_assertions += other.total_assertions;
  passed_assertions += other.passed_assertions;
  return *this;
}

[[gnu::noinline]] std::ostream& operator<<(std::ostream& os, test_metrics const& m) noexcept {
  os << "IMPORTANT NOTE: reports below do not necessarily mean all tests ran. See any error messages above!" << '\n'
     << "Tests: " << m.passed_tests << '/' << m.total_tests << " [Failed " << m.failed_tests << " test(s)]" << '\n'
     << "Points: " << m.earned_points << '/' << m.total_points << '\n'
     << "Assertions: " << m.passed_assertions << '/' << m.total_assertions << '\n';
  return os;
}

[[gnu::noinline]] void test_metrics::reset() noexcept {
  total_points      = 0;
  earned_points     = 0;
  total_tests       = 0;
  failed_tests      = 0;
  passed_tests      = 0;
  total_assertions  = 0;
  passed_assertions = 0;
}

namespace serialization::test_metrics {
[[gnu::noinline]] void write(::test_metrics const& m, int fd) noexcept {
  ::write(fd, &m, sizeof(::test_metrics));
}
[[gnu::noinline]] ::test_metrics read(int fd) noexcept {
  ::test_metrics m;
  ::read(fd, &m, sizeof(::test_metrics));
  return m;
}
} // namespace serialization::test_metrics

namespace serialization::string {
[[gnu::noinline]] void write(std::string const& str, int fd) noexcept {
  int sz = static_cast<int>(str.size());
  ::write(fd, &sz, sizeof(sz));
  ::write(fd, str.data(), sz);
}
[[gnu::noinline]] std::string read(int fd) noexcept {
  int sz;
  ::read(fd, &sz, sizeof(sz));
  std::string res;
  res.resize(sz);
  ::read(fd, std::addressof(res[0]), sz);
  return res;
}
} // namespace serialization::string

namespace serialization::boolean {
[[gnu::noinline]] void write(bool const& b, int fd) noexcept {
  ::write(fd, &b, sizeof(b));
}
[[gnu::noinline]] bool read(int fd) noexcept {
  bool b;
  ::read(fd, &b, sizeof(b));
  return b;
}
} // namespace serialization::boolean

[[gnu::noinline]] std::string framework::make_message_stack() const noexcept {
  std::string msg;
  for (auto const& s : description_stack) {
    msg += s;
    msg += '\n';
  }
  return msg;
}

[[gnu::noinline]] void framework::send(bool const& flag) const noexcept {
  packet_header header{packet_header::boolean};
  write(snd_fd, &header, sizeof(packet_header));
  serializable_bool{flag}.write(snd_fd);
}

[[gnu::noinline]] void framework::send(std::string const& msg) const noexcept {
  packet_header header{packet_header::string};
  write(snd_fd, &header, sizeof(packet_header));
  serializable_string{msg}.write(snd_fd);
}

[[gnu::noinline]] void framework::send(test_metrics const& met) const noexcept {
  packet_header header{packet_header::test_metrics};
  write(snd_fd, &header, sizeof(packet_header));
  serializable_test_metrics{met}.write(snd_fd);
}

[[gnu::noinline]] serializable_types framework::receive() const noexcept {
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

[[gnu::noinline]] void framework::child(int points, std::function<void()> nesting) noexcept {
  // child
  ++level;

  points_specified = points_specified || (points != 0);
  passed           = std::nullopt;
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

[[gnu::noinline]] void framework::parent(int points, int pid) noexcept {
  std::optional<std::string> additional_msg{std::nullopt};

  auto const start = std::chrono::steady_clock::now();

  points_specified = points_specified || (points != 0);
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
    } else if (res == 0 and points_specified) {
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
  if (level > 0) {
    send(passed.value_or(false));
  }
  description_stack.pop_back();
}

[[gnu::noinline]] framework::~framework() noexcept {
  if (level == 0) {
    std::cout << metrics << std::endl;
  }
}

[[gnu::noinline]] int framework::status() const noexcept {
  return passed.value_or(false) ? 0 : 1;
}

[[gnu::noinline]] void framework::require(std::string const& description, bool condition) noexcept {
  ++metrics.total_assertions;
  if (not condition) {
    passed = false;
  } else {
    passed = passed.value_or(true);
    ++metrics.passed_assertions;
  }
  if (not condition || verbose) {
    send(make_message_stack() + (condition ? "PASS: " : "FAIL: ") + description + '\n' + '\n');
  }
}

[[gnu::noinline]] framework::framework(bool verbose) noexcept : verbose{verbose} {
}
[[gnu::noinline]] framework::framework(int time_limit) noexcept : time_limit(time_limit) {
}
[[gnu::noinline]] framework::framework(bool verbose, int time_limit) noexcept : verbose(verbose), time_limit(time_limit) {
}