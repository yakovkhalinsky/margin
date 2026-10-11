// SPDX-License-Identifier: MIT
#include "test.h"

namespace mt {
std::vector<Case>& registry() {
  static std::vector<Case> r;
  return r;
}
int failures = 0;
std::string hex(const unsigned char* p, size_t n) {
  static const char* d = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; ++i) {
    s += d[p[i] >> 4];
    s += d[p[i] & 15];
  }
  return s;
}
}  // namespace mt

int main() {
  int failedCases = 0;
  for (auto& c : mt::registry()) {
    const int before = mt::failures;
    c.fn();
    const bool ok = mt::failures == before;
    if (!ok) ++failedCases;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", c.name);
  }
  std::printf("\n%zu tests, %d failed\n", mt::registry().size(), failedCases);
  return failedCases ? 1 : 0;
}
