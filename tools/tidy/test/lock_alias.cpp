// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-lock-alias
#include <reloco/atomic_context.hpp>

class RELOCO_ATOMIC_LOCK spin {
public:
  RELOCO_ENTER_ATOMIC void lock() noexcept;
  RELOCO_LEAVE_ATOMIC void unlock() noexcept;
};

// An RAII guard is a lock implementation: it may keep a pointer to the lock.
class RELOCO_ATOMIC_SCOPE guard {
public:
  explicit guard(spin &l) noexcept : m_(&l) { m_->lock(); }
  ~guard() noexcept { m_->unlock(); }

private:
  spin *m_;
};

struct owner {
  spin lock_;
  spin *bad_field_; // expect: reloco-lock-alias
  spin &ref_;       // references are fine: they name one lock for their whole life
};

void ok_reference(spin &l) noexcept {
  spin &alias = l;
  alias.lock();
  alias.unlock();
}

void bad_param(spin *l) noexcept { // expect: reloco-lock-alias
  (void)l;
}

void bad_local(spin &l) noexcept {
  spin *p = &l; // expect: reloco-lock-alias
  (void)p;
}

void bad_address(spin &l) noexcept {
  (void)&l; // expect: reloco-lock-alias
}

void ok_direct(owner &o) noexcept {
  o.lock_.lock();
  o.lock_.unlock();
}

RELOCO_LOCK_IMPL void exempt_helper(spin *l) noexcept { (void)l; }

void nolint(spin &l) noexcept {
  spin *p = &l; // NOLINT(reloco-lock-alias)
  (void)p;
}

// Legacy lock types are passed by pointer all over C APIs; RELOCO_LEGACY_LOCK accepts that.
struct __attribute__((capability("mutex"))) RELOCO_LEGACY_LOCK legacy_mtx {
  int owner;
};
void legacy_lock(legacy_mtx *m) noexcept;
void ok_legacy_pointer(legacy_mtx &m) noexcept { legacy_lock(&m); }

// A shim header forward-declares the type with the capability before including the legacy header, so the legacy
// header needs no edit; the later definition inherits the attribute and the type is a lock.
struct __attribute__((capability("mutex"))) shimmed_mtx;
struct shimmed_mtx {
  int owner;
};
void bad_shimmed_pointer(shimmed_mtx *m) noexcept; // expect: reloco-lock-alias
