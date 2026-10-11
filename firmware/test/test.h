// SPDX-License-Identifier: MIT
// Minimal dependency-free test harness.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace mt {
struct Case {
  const char* name;
  std::function<void()> fn;
};
std::vector<Case>& registry();
extern int failures;
struct Reg {
  Reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};
std::string hex(const unsigned char* p, size_t n);
}  // namespace mt

#define MT_CAT2(a, b) a##b
#define MT_CAT(a, b) MT_CAT2(a, b)
#define TEST(name)                                         \
  static void MT_CAT(test_, name)();                       \
  static mt::Reg MT_CAT(reg_, name)(#name, MT_CAT(test_, name)); \
  static void MT_CAT(test_, name)()

#define CHECK(cond)                                                               \
  do {                                                                            \
    if (!(cond)) {                                                                \
      std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
      ++mt::failures;                                                             \
    }                                                                             \
  } while (0)

#define CHECK_EQ(a, b)                                                                                     \
  do {                                                                                                     \
    auto _a = (a);                                                                                         \
    auto _b = (b);                                                                                         \
    if (!(_a == _b)) {                                                                                     \
      std::fprintf(stderr, "  FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b);                       \
      ++mt::failures;                                                                                      \
    }                                                                                                      \
  } while (0)
