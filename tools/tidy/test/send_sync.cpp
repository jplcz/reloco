// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-send-sync
#include <atomic>
#include <reloco/detail/compat.hpp>
#include <reloco/lifetime.hpp>
#include <vector>

template <typename F> void spawn(F fn RELOCO_REQUIRE_SEND) noexcept { (void)fn; }
template <typename T> void share(const T &v RELOCO_REQUIRE_SYNC) noexcept;
void consume(int *p RELOCO_REQUIRE_SEND) noexcept;

struct plain {
  int a;
  long b;
};
struct holds_pointer {
  int *p;
};
struct RELOCO_SEND audited {
  int *p;
};
struct RELOCO_NOT_SEND affine {
  int tid;
};
struct RELOCO_CAPABILITY("mutex") fake_mutex {};
struct locked {
  fake_mutex mu;
  int v RELOCO_GUARDED_BY(mu);
};
struct racy {
  int v;
};

void ok_send(int x, std::vector<int> v) noexcept {
  spawn([x, v = static_cast<std::vector<int> &&>(v)]() noexcept { (void)x; (void)v; });
  spawn(plain{1, 2});
  spawn(audited{nullptr});
}
void ok_sync(const locked &l, const plain &p, std::atomic<int> &a) noexcept {
  share(l);
  share(p);
  share(a);
}

void bad_send(int &r, int *ptr) noexcept {
  spawn([&r]() noexcept { (void)r; });  // expect: reloco-send-sync
  spawn(holds_pointer{ptr});  // expect: reloco-send-sync
  spawn(affine{1});  // expect: reloco-send-sync
  consume(ptr);  // expect: reloco-send-sync
}
void bad_sync(racy &r) noexcept {
  share(r);  // expect: reloco-send-sync
}
void suppressed(int *ptr) noexcept {
  spawn(holds_pointer{ptr});  // NOLINT(reloco-send-sync)
}
