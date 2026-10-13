// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-banned-construct
#include <cstring>
#include <reloco/lifetime.hpp>

void f(int *p, const void *q, char *d) noexcept {
  int *n = new int(1);  // expect: reloco-banned-construct
  delete n;  // expect: reloco-banned-construct
  auto *c = const_cast<int *>(p);  // expect: reloco-banned-construct
  auto r = reinterpret_cast<const char *>(q);  // expect: reloco-banned-construct
  auto s = (long)p;  // expect: reloco-banned-construct
  memcpy(d, r, 1);  // expect: reloco-banned-construct
  (void)c; (void)s;
}
RELOCO_UNSAFE_BUFFER_USAGE void unsafe_copy(char *d, const void *q) noexcept {
  memcpy(d, q, 1);
  (void)reinterpret_cast<const char *>(q);
}
// The reloco::unsafe namespace counts as the unsafe tier.
namespace reloco::unsafe {
inline const char *punned(const void *q) noexcept { return reinterpret_cast<const char *>(q); }
}  // namespace reloco::unsafe

// A non-type template argument of enum type is converted by a cast the compiler synthesises in the instantiation.
enum class rdx : unsigned { two = 2, ten = 10 };
template <rdx R> int pick() noexcept { return R == rdx::two ? 1 : 2; }
inline int use_pick() noexcept { return pick<rdx::ten>(); }
