#pragma once

#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <unistd.h>

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

  [[gnu::noinline]] test_metrics& operator+=(test_metrics const& other) noexcept;
  friend std::ostream&            operator<<(std::ostream& os, test_metrics const& m) noexcept;
  [[gnu::noinline]] void          reset() noexcept;
};

namespace serialization::test_metrics {
[[gnu::noinline]] void           write(::test_metrics const& m, int fd) noexcept;
[[gnu::noinline]] ::test_metrics read(int fd) noexcept;
} // namespace serialization::test_metrics

namespace serialization::string {
[[gnu::noinline]] void        write(std::string const& str, int fd) noexcept;
[[gnu::noinline]] std::string read(int fd) noexcept;
} // namespace serialization::string

namespace serialization::boolean {
[[gnu::noinline]] void write(bool const& b, int fd) noexcept;
[[gnu::noinline]] bool read(int fd) noexcept;
} // namespace serialization::boolean

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
template <> struct type_for<packet_header::string> { using type = serializable_string; };
template <> struct type_for<packet_header::test_metrics> { using type = serializable_test_metrics; };
} // namespace detail
template <packet_header PH> using type_for = typename detail::type_for<PH>::type;

template <typename... OverloadSet> struct Overload : OverloadSet... { using OverloadSet::operator()...; };
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

  [[gnu::noinline]] std::string        make_message_stack() const noexcept;
  [[gnu::noinline]] void               send(bool const& flag) const noexcept;
  [[gnu::noinline]] void               send(std::string const& msg) const noexcept;
  [[gnu::noinline]] void               send(test_metrics const& met) const noexcept;
  [[gnu::noinline]] serializable_types receive() const noexcept;
  [[gnu::noinline]] void               child(int points, std::function<void()> nesting) noexcept;
  [[gnu::noinline]] void               parent(int points, int pid) noexcept;

public:
  [[gnu::noinline]] framework(bool verbose) noexcept;
  [[gnu::noinline]] framework(int time_limit) noexcept;
  [[gnu::noinline]] framework(bool verbose, int time_limit) noexcept;

  framework() noexcept                   = default;
  framework(framework const&)            = delete;
  framework(framework&&)                 = delete;
  framework& operator=(framework const&) = delete;
  framework& operator=(framework&&)      = delete;

  [[gnu::noinline]] ~framework() noexcept;

  [[gnu::noinline]] int status() const noexcept;

  [[gnu::noinline]] void require(std::string const& description, bool condition) noexcept;

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
      parent(points, pid);
    }
  }

  template <typename Callable> void operator()(std::string const& description, Callable&& nesting) noexcept {
    (*this)(0, description, std::forward<Callable>(nesting));
  }
};
