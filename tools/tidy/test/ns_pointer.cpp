// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-ns-pointer
#include <cstdint>
#include <reloco/lifetime.hpp>

using user_ptr = char *RELOCO_NS(user);
using guest_ptr = char *RELOCO_NS(guest);

void takes_plain(char *p) noexcept;
void takes_user(char *RELOCO_NS(user) p) noexcept;

// The translator may inspect and convert; callers get a real pointer back.
RELOCO_NS_TRANSLATOR char *translate(char *RELOCO_NS(user) p) noexcept { return reinterpret_cast<char *>(p) + *p * 0; }

struct request {
  char *RELOCO_NS(user) buffer;
  unsigned long size;
};

user_ptr make(std::uintptr_t address) noexcept { return reinterpret_cast<user_ptr>(address); }

void ok_pass_along(user_ptr p, request &r) noexcept {
  takes_user(p);
  takes_user(r.buffer);
  takes_user(p + 4);
  char *real = translate(p);
  takes_plain(real);
  *real = 0;
  if (p == nullptr)
    return;
}

void bad_deref(user_ptr p) noexcept {
  char c = *p;  // expect: reloco-ns-pointer
  (void)c;
}
void bad_subscript(user_ptr p) noexcept {
  p[1] = 0;  // expect: reloco-ns-pointer
}
void bad_member(request *r) noexcept {
  char c = r->buffer[0];  // expect: reloco-ns-pointer
  (void)c;
}
void bad_to_plain(user_ptr p) noexcept {
  takes_plain(p);  // expect: reloco-ns-pointer
}
void bad_cross_tag(guest_ptr g) noexcept {
  takes_user(g);  // expect: reloco-ns-pointer
}
void bad_cast(user_ptr p) noexcept {
  auto *q = reinterpret_cast<char *>(p);  // expect: reloco-ns-pointer
  (void)q;
}
void bad_assign(user_ptr p, char *plain) noexcept {
  p = plain;  // expect: reloco-ns-pointer
}
char *bad_return(user_ptr p) noexcept {
  return p;  // expect: reloco-ns-pointer
}
void suppressed(user_ptr p) noexcept {
  takes_plain(p);  // NOLINT(reloco-ns-pointer)
}
