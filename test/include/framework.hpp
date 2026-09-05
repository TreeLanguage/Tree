#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace tf {

struct TestCase {
  const char *name;
  void (*fn)();
};

inline std::vector<TestCase> &registry() {
  static std::vector<TestCase> tests;
  return tests;
}

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

struct Registrar {
  Registrar(const char *name, void (*fn)()) {
    registry().push_back({name, fn});
  }
};

inline int run_all() {
  for (const TestCase &tc : registry()) {
    current_test_name() = tc.name;
    std::cerr << "[RUN ] " << tc.name << '\n';
    tc.fn();
  }

  if (failures() == 0) {
    std::cerr << "All " << registry().size() << " test(s) passed.\n";
  } else {
    std::cerr << failures() << " check(s) failed across " << registry().size()
              << " test(s).\n";
  }
  return failures();
}

} // namespace tf

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      tf::report_failure(__FILE__, __LINE__, "CHECK failed: " #cond);          \
    }                                                                          \
  } while (0)

#define CHECK_EQ(actual, expected)                                             \
  do {                                                                         \
    const auto &actual_val = (actual);                                         \
    const auto &expected_val = (expected);                                     \
    if (!(actual_val == expected_val)) {                                       \
      std::ostringstream oss;                                                  \
      oss << #actual << " == " << #expected << " (got " << actual_val          \
          << ", expected " << expected_val << ')';                             \
      tf::report_failure(__FILE__, __LINE__, oss.str());                       \
    }                                                                          \
  } while (0)

#define TEST(name)                                                             \
  static void tf_test_##name();                                                \
  namespace {                                                                  \
  static ::tf::Registrar tf_registrar_##name(#name, &tf_test_##name);          \
  }                                                                            \
  static void tf_test_##name()
