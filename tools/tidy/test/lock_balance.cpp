// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-lock-balance
#include <reloco/atomic_context.hpp>

class RELOCO_ATOMIC_LOCK spin {
public:
  void lock() noexcept;
  void unlock() noexcept;
  [[nodiscard]] bool try_lock() noexcept;
  void read_lock() noexcept;
  void read_unlock() noexcept;
};

// Clang capability class with explicit acquire/release attributes (no name matching needed).
class __attribute__((capability("mutex"))) cap {
public:
  void take() noexcept __attribute__((acquire_capability()));
  void drop() noexcept __attribute__((release_capability()));
};

// An RAII guard is a lock implementation: acquiring in the constructor and releasing in the destructor is fine.
class RELOCO_ATOMIC_SCOPE guard {
public:
  explicit guard(spin &l) noexcept : l_(l) { l_.lock(); }
  ~guard() noexcept { l_.unlock(); }

private:
  spin &l_;
};

void ok_balanced(spin &l) noexcept {
  l.lock();
  l.unlock();
}

void ok_guard(spin &l) noexcept { guard g(l); }

void ok_all_paths(spin &l, bool f) noexcept {
  l.lock();
  if (f) {
    l.unlock();
    return;
  }
  l.unlock();
}

void leak_simple(spin &l) noexcept {
  l.lock(); // expect: reloco-lock-balance
}

void leak_one_path(spin &l, bool f) noexcept {
  l.lock(); // expect: reloco-lock-balance
  if (f)
    return;
  l.unlock();
}

void leak_loop(spin &l, int n) noexcept {
  for (int i = 0; i < n; ++i)
    l.lock(); // expect: reloco-lock-balance
}

void leak_shared(spin &l) noexcept {
  l.read_lock(); // expect: reloco-lock-balance
}

void ok_shared(spin &l) noexcept {
  l.read_lock();
  l.read_unlock();
}

void leak_attr(cap &c) noexcept {
  c.take(); // expect: reloco-lock-balance
}

void ok_attr(cap &c) noexcept {
  c.take();
  c.drop();
}

void ok_try_negated(spin &l) noexcept {
  if (!l.try_lock())
    return;
  l.unlock();
}

void ok_try_positive(spin &l) noexcept {
  if (l.try_lock()) {
    l.unlock();
  }
}

void leak_try(spin &l) noexcept {
  if (l.try_lock()) { // expect: reloco-lock-balance
    return;
  }
}

void ok_try_loop(spin &l) noexcept {
  while (!l.try_lock()) {
  }
  l.unlock();
}

void double_lock(spin &l) noexcept {
  l.lock();
  l.lock(); // expect: reloco-lock-balance
  l.unlock();
}

void double_unlock(spin &l) noexcept {
  l.lock();
  l.unlock();
  l.unlock(); // expect: reloco-lock-balance
}

// Hand-off: unlocking a lock the caller took is not reported.
void ok_unlock_only(spin &l) noexcept { l.unlock(); }

struct owner {
  spin lock_;
  spin other_;
  void ok_member() noexcept {
    lock_.lock();
    lock_.unlock();
  }
  void leak_member() noexcept {
    lock_.lock(); // expect: reloco-lock-balance
  }
  // Locks are tracked per place: releasing a different lock does not balance this one.
  void wrong_lock() noexcept {
    lock_.lock(); // expect: reloco-lock-balance
    other_.unlock();
  }
};

void ok_reference_alias(owner &o) noexcept {
  spin &l = o.lock_;
  l.lock();
  o.lock_.unlock();
}

void ok_through_pointer_member(owner *o) noexcept {
  o->lock_.lock();
  o->lock_.unlock();
}

// A function marked as a lock implementation may return with the lock held.
RELOCO_LOCK_IMPL void acquire_for_caller(spin &l) noexcept { l.lock(); }

void nolint(spin &l) noexcept {
  l.lock(); // NOLINT(reloco-lock-balance)
}

void lambda_leak(spin &l) noexcept {
  auto f = [&l]() noexcept {
    l.lock(); // expect: reloco-lock-balance
  };
  f();
}

// A capability-annotated wrapper that returns a guard releases through the guard, so the call is not an open acquire.
class __attribute__((capability("mutex"))) guarded {
public:
  [[nodiscard]] guard lock() noexcept __attribute__((acquire_capability()));
};

void ok_guard_returning(guarded &g) noexcept { auto h = g.lock(); (void)h; }

// A failed-or-unchecked try_lock on an already held lock must not leave the original acquire open.
void ok_try_lock_while_held(spin &l) noexcept {
  l.lock();
  (void)l.try_lock();
  l.unlock();
}

[[noreturn]] void die() noexcept;

// A path that ends in a noreturn call never returns, so holding the lock there is not a leak.
void ok_noreturn_path(spin &l, bool bad) noexcept {
  l.lock();
  if (bad)
    die();
  l.unlock();
}

// Locks extended by derivation or composition are still locks; only the innermost one needs the annotation.
class derived_spin : public spin {};
class wrapped_spin {
public:
  RELOCO_LOCK_IMPL void lock() noexcept { m_.lock(); }
  RELOCO_LOCK_IMPL void unlock() noexcept { m_.unlock(); }

private:
  spin m_;
};

void ok_derived(derived_spin &l) noexcept {
  l.lock();
  l.unlock();
}

void bad_derived(derived_spin &l) noexcept {
  l.lock(); // expect: reloco-lock-balance
}

void bad_wrapped(wrapped_spin &l) noexcept {
  l.lock(); // expect: reloco-lock-balance
}

// Legacy C-style lock API: free functions name the lock through a parameter.
struct mtx {
  int owner;
};
RELOCO_ACQUIRES(0) void mtx_lock(struct mtx *m) noexcept;
RELOCO_RELEASES(0) void mtx_unlock(struct mtx *m) noexcept;
RELOCO_TRY_ACQUIRES(0) int mtx_trylock(struct mtx *m) noexcept;

void ok_free_balanced(mtx &m) noexcept {
  mtx_lock(&m);
  mtx_unlock(&m);
}

void ok_free_pointer_param(mtx *m) noexcept {
  mtx_lock(m);
  mtx_unlock(m);
}

void bad_free_leak(mtx &m, bool early) noexcept {
  mtx_lock(&m); // expect: reloco-lock-balance
  if (early)
    return;
  mtx_unlock(&m);
}

void bad_free_double_lock(mtx &m) noexcept {
  mtx_lock(&m);
  mtx_lock(&m); // expect: reloco-lock-balance
  mtx_unlock(&m);
}

void ok_free_trylock(mtx &m) noexcept {
  if (!mtx_trylock(&m))
    return;
  mtx_unlock(&m);
}

void bad_free_trylock_leak(mtx &m) noexcept {
  if (mtx_trylock(&m)) // expect: reloco-lock-balance
    return;
}

// A legacy wrapper that hands the lock to its caller is itself annotated, so its body is not checked.
RELOCO_ACQUIRES(0) void my_mtx_lock(struct mtx *m) noexcept { mtx_lock(m); }
