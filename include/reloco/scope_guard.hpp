#pragma once
#include <type_traits>
#include <utility>

namespace reloco {

template <typename Callable> class scope_guard {
public:
  // Store the callable inline on the stack (Zero allocation)
  constexpr explicit scope_guard(Callable &&fn) noexcept : m_fn(std::forward<Callable>(fn)) {}

  // Banned copying
  scope_guard(const scope_guard &) = delete;
  scope_guard &operator=(const scope_guard &) = delete;

  // Move semantics transfer the responsibility
  constexpr scope_guard(scope_guard &&other) noexcept : m_fn(std::move(other.m_fn)), m_active(other.m_active) {
    other.m_active = false;
  }

  constexpr scope_guard &operator=(scope_guard &&other) noexcept {
    if (this != &other) {
      m_fn = std::move(other.m_fn);
      m_active = other.m_active;
      other.m_active = false;
    }
    return *this;
  }

  // Executes the payload deterministically on scope exit
  RELOCO_CONSTEXPR20 ~scope_guard() noexcept {
    if (m_active) {
      m_fn();
    }
  }

  constexpr void cancel() noexcept { m_active = false; }

private:
  Callable m_fn;
  bool m_active{true};
};

// C++17 Class Template Argument Deduction (CTAD)
template <typename Callable> scope_guard(Callable) -> scope_guard<Callable>;

} // namespace reloco

// ========================================================================
// THE MACRO: Prevents accidental temporary creation
// ========================================================================
// If a user writes `reloco::scope_guard([]{});` without assigning it to a
// variable, it executes immediately! This macro forces a unique local variable.
#define RELOCO_CONCAT_IMPL(s1, s2) s1##s2
#define RELOCO_CONCAT(s1, s2) RELOCO_CONCAT_IMPL(s1, s2)
#define RELOCO_DEFER(fn) ::reloco::scope_guard RELOCO_CONCAT(reloco_defer_, __LINE__)(fn)