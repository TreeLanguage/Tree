#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace tf {

struct TestBase {
  const char *name;

  explicit TestBase(const char *n) noexcept : name(n) {
    registry().push_back(this);
  }
  virtual ~TestBase() = default;

  TestBase(const TestBase &) = delete;
  TestBase &operator=(const TestBase &) = delete;
  TestBase(TestBase &&) = delete;
  TestBase &operator=(TestBase &&) = delete;

  virtual void run() = 0;

  static std::vector<TestBase *> &registry() {
    static std::vector<TestBase *> tests;
    return tests;
  }
};

inline int &failures() {
  static int count = 0;
  return count;
}

inline const char *&current_test_name() {
  static const char *name = "";
  return name;
}

inline void report_failure(const char *file, int line, const std::string &msg) {
  failures()++;
  std::cerr << "[FAIL] " << current_test_name() << " (" << file << ':' << line
            << "): " << msg << '\n';
}

inline int run_all() {
  for (TestBase *tc : TestBase::registry()) {
    current_test_name() = tc->name;
    std::cerr << "[RUN ] " << tc->name << '\n';
    tc->run();
  }

  if (failures() == 0) {
    std::cerr << "All " << TestBase::registry().size() << " test(s) passed.\n";
  } else {
    std::cerr << failures() << " check(s) failed across "
              << TestBase::registry().size() << " test(s).\n";
  }
  return failures();
}

} // namespace tf

#define CHECK(cond)                                                            \
  [&] {                                                                        \
    if (!(cond)) {                                                             \
      tf::report_failure(__FILE__, __LINE__, "CHECK failed: " #cond);          \
    }                                                                          \
  }()

#define CHECK_EQ(actual, expected)                                             \
  [&] {                                                                        \
    const auto &actual_val = (actual);                                         \
    const auto &expected_val = (expected);                                     \
    if (!(actual_val == expected_val)) {                                       \
      std::ostringstream oss;                                                  \
      oss << #actual << " == " << #expected << " (got " << actual_val          \
          << ", expected " << expected_val << ')';                             \
      tf::report_failure(__FILE__, __LINE__, oss.str());                       \
    }                                                                          \
  }()

#define TEST(name)                                                             \
  namespace {                                                                  \
  struct Test_##name : ::tf::TestBase {                                        \
    Test_##name() noexcept : ::tf::TestBase(#name) {}                          \
    void run() override;                                                       \
  };                                                                           \
  Test_##name tf_instance_##name;                                              \
  }                                                                            \
  void Test_##name::run()
