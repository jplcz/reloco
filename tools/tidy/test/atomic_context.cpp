// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-atomic-context
#include <reloco/atomic_context.hpp>

// An RAII guard: constructing it starts an atomic scope that ends at the end of the enclosing block.
class RELOCO_ATOMIC_SCOPE irq_guard {
public:
  irq_guard() noexcept;
  ~irq_guard() noexcept;
  irq_guard(const irq_guard &) = delete;
};
// A proof that atomic context is held.
class RELOCO_ATOMIC_TOKEN token {};

RELOCO_MIGHT_SLEEP void block_on_io() noexcept;
RELOCO_ATOMIC_ONLY int *cpu_local() noexcept;
RELOCO_ENTER_ATOMIC void preempt_off() noexcept;
RELOCO_LEAVE_ATOMIC void preempt_on() noexcept;
template <typename F> void with_irq_disabled(F fn RELOCO_ATOMIC_CALLBACK) noexcept;

// A class marked as sleeping makes every member sleep.
class RELOCO_MIGHT_SLEEP mutex {
public:
  void lock() noexcept;
};

void ok_sleep_outside() noexcept {
  block_on_io();
}
void bad_sleep_in_scope() noexcept {
  irq_guard g;
  block_on_io();  // expect: reloco-atomic-context
}
void ok_scope_ended() noexcept {
  {
    irq_guard g;
    (void)cpu_local();
  }
  block_on_io();
}
void bad_class_marked(mutex &m) noexcept {
  irq_guard g;
  m.lock();  // expect: reloco-atomic-context
}
void bad_scope_in_branch(bool c) noexcept {
  if (c) {
    irq_guard g;
    (void)cpu_local();
  }
  (void)cpu_local();  // expect: reloco-atomic-context
}
void bad_early_return_keeps_scope_until_return(bool c) noexcept {
  irq_guard g;
  if (c)
    return;
  block_on_io();  // expect: reloco-atomic-context
}

// Functions that need atomic context.
void bad_cpu_local_unprotected() noexcept {
  (void)cpu_local();  // expect: reloco-atomic-context
}
void ok_cpu_local_guarded() noexcept {
  irq_guard g;
  (void)cpu_local();
}
RELOCO_ATOMIC_ONLY void ok_atomic_only_caller() noexcept {
  (void)cpu_local();
}
RELOCO_ATOMIC_ONLY void bad_atomic_only_sleeps() noexcept {
  block_on_io();  // expect: reloco-atomic-context
}
void bad_call_atomic_only_unprotected() noexcept {
  ok_atomic_only_caller();  // expect: reloco-atomic-context
}
RELOCO_ATOMIC_ENTRY void isr() noexcept {
  (void)cpu_local();
  block_on_io();  // expect: reloco-atomic-context
}

// Explicit enter/leave pairs.
void ok_enter_leave() noexcept {
  preempt_off();
  (void)cpu_local();
  preempt_on();
  block_on_io();
}
void bad_enter_leave() noexcept {
  preempt_off();
  block_on_io();  // expect: reloco-atomic-context
  preempt_on();
}

// Proof tokens make the whole function atomic.
void ok_with_token(token) noexcept {
  (void)cpu_local();
}
void bad_token_sleeps(token) noexcept {
  block_on_io();  // expect: reloco-atomic-context
}

// Atomic callbacks.
void callbacks() noexcept {
  with_irq_disabled([] { (void)cpu_local(); });
  with_irq_disabled([] {
    block_on_io();  // expect: reloco-atomic-context
  });
}
// A lambda created in an atomic scope is assumed to run in it.
void lambda_inherits() noexcept {
  irq_guard g;
  auto f = [] { block_on_io(); };  // expect: reloco-atomic-context
  f();
}

// Transitive inference over visible bodies.
void helper_sleeps() noexcept {
  block_on_io();
}
void middle() noexcept {
  helper_sleeps();
}
void bad_transitive() noexcept {
  irq_guard g;
  middle();  // expect: reloco-atomic-context
}
void helper_pure() noexcept {}
void ok_not_sleeping() noexcept {
  irq_guard g;
  helper_pure();
}

// Exemptions.
RELOCO_ATOMIC_EXEMPT void sleeps_only_with_flag() noexcept {
  block_on_io();
}
void ok_exempt_callee() noexcept {
  irq_guard g;
  sleeps_only_with_flag();
}
void ok_nolint() noexcept {
  irq_guard g;
  block_on_io();  // NOLINT(reloco-atomic-context): wakes a thread, never waits here
}
RELOCO_MIGHT_SLEEP RELOCO_ATOMIC_ONLY void contradictory() noexcept;  // expect: reloco-atomic-context

// Generic guards: a scope only when specialised on an atomic lock.
class RELOCO_ATOMIC_LOCK spin {
public:
  RELOCO_ENTER_ATOMIC void lock() noexcept;
  RELOCO_LEAVE_ATOMIC void unlock() noexcept;
};
class plain_mutex {
public:
  void lock() noexcept;
  void unlock() noexcept;
};
template <typename L> class RELOCO_ATOMIC_GUARD lock_guard {
public:
  explicit lock_guard(L &l) noexcept : l_(l) { l_.lock(); }
  ~lock_guard() noexcept { l_.unlock(); }
  lock_guard(const lock_guard &) = delete;

private:
  L &l_;
};
void bad_guard_over_spin(spin &s) noexcept {
  lock_guard<spin> g(s);
  block_on_io();  // expect: reloco-atomic-context
}
void ok_guard_over_plain_mutex(plain_mutex &m) noexcept {
  lock_guard<plain_mutex> g(m);
  block_on_io();
}
void bad_direct_spin(spin &s) noexcept {
  s.lock();
  block_on_io();  // expect: reloco-atomic-context
  s.unlock();
}

// A lock extended by derivation or composition stays an atomic lock without repeating the annotation.
class derived_spin : public spin {};
class wrapped_spin {
public:
  void lock() noexcept { m_.lock(); }
  void unlock() noexcept { m_.unlock(); }

private:
  spin m_;
};
class not_a_lock_wrapper {
public:
  void lock() noexcept { m_.lock(); }

private:
  plain_mutex m_;
};
void bad_guard_over_derived(derived_spin &s) noexcept {
  lock_guard<derived_spin> g(s);
  block_on_io();  // expect: reloco-atomic-context
}
void bad_guard_over_wrapped(wrapped_spin &s) noexcept {
  lock_guard<wrapped_spin> g(s);
  block_on_io();  // expect: reloco-atomic-context
}
void ok_guard_over_wrapped_mutex(not_a_lock_wrapper &s) noexcept {
  lock_guard<not_a_lock_wrapper> g(s);
  block_on_io();
}
