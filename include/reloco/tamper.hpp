
#pragma once
#include "masked_byte_region.hpp"

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

namespace security {

#if !defined(RELOCO_KERNEL)
/** @brief Provides the integrity cookies used by tamper-protected values. */
struct os_integer_region_cookie {
  static inline once_lock<uint64_t> cookie{};
  [[nodiscard]] static inline uint64_t integer_region_cookie() noexcept {
    return cookie.get_or_init(detail::generate_weak_region_cookie);
  }
  static inline once_lock<uint64_t> cookie1{};
  [[nodiscard]] static inline uint64_t integer_region_cookie1() noexcept {
    return cookie1.get_or_init(detail::generate_weak_region_cookie);
  }
  static inline once_lock<uint64_t> cookie2{};
  [[nodiscard]] static inline uint64_t integer_region_cookie2() noexcept {
    return cookie2.get_or_init(detail::generate_weak_region_cookie);
  }
};
#else
/** @brief Provides the integrity cookies used by tamper-protected values. */
struct os_integer_region_cookie {
  [[nodiscard]] static uint64_t integer_region_cookie() noexcept;
  [[nodiscard]] static uint64_t integer_region_cookie1() noexcept;
  [[nodiscard]] static uint64_t integer_region_cookie2() noexcept;
};
#endif
} // namespace security

/**
 * @brief Stores an integral value in an XOR-masked representation.
 * @tparam T Integral value type.
 */
template <typename T, typename std::enable_if_t<std::is_integral_v<T>, int> = 0> class masked_integral {
  T obfuscated_val_;

  [[nodiscard]] inline T load() const noexcept {
    return obfuscated_val_ ^ static_cast<T>(security::os_integer_region_cookie::integer_region_cookie());
  }

  inline void store(T val) noexcept {
    obfuscated_val_ = val ^ static_cast<T>(security::os_integer_region_cookie::integer_region_cookie());
  }

public:
  masked_integral() noexcept { store(0); }

  // Implicit constructor allows: masked_integral<int> x = 5;
  masked_integral(T val) noexcept { store(val); }

  // Assignment
  masked_integral &operator=(T val) noexcept {
    store(val);
    return *this;
  }

  // Transparent read (handles ==, !=, +, -, etc. implicitly)
  [[nodiscard]] operator T() const noexcept { return load(); }

  // ---- Compound Arithmetic ----
  masked_integral &operator+=(T val) noexcept {
    store(load() + val);
    return *this;
  }

  masked_integral &operator-=(T val) noexcept {
    store(load() - val);
    return *this;
  }

  masked_integral &operator*=(T val) noexcept {
    store(load() * val);
    return *this;
  }

  masked_integral &operator/=(T val) noexcept {
    store(load() / val);
    return *this;
  }

  masked_integral &operator%=(T val) noexcept {
    store(load() % val);
    return *this;
  }

  // ---- Compound Bitwise ----
  masked_integral &operator&=(T val) noexcept {
    store(load() & val);
    return *this;
  }

  masked_integral &operator|=(T val) noexcept {
    store(load() | val);
    return *this;
  }

  masked_integral &operator^=(T val) noexcept {
    store(load() ^ val);
    return *this;
  }

  masked_integral &operator<<=(T val) noexcept {
    store(load() << val);
    return *this;
  }

  masked_integral &operator>>=(T val) noexcept {
    store(load() >> val);
    return *this;
  }

  // ---- Prefix Increment/Decrement ----
  masked_integral &operator++() noexcept {
    store(load() + 1);
    return *this;
  }

  masked_integral &operator--() noexcept {
    store(load() - 1);
    return *this;
  }

  // ---- Postfix Increment/Decrement ----
  T operator++(int) noexcept {
    T old = load();
    store(old + 1);
    return old;
  }

  T operator--(int) noexcept {
    T old = load();
    store(old - 1);
    return old;
  }
};

/**
 * @brief Stores an enum or boolean state with dual masked representations.
 * @tparam EnumT Enumeration or boolean type.
 */
template <typename EnumT> class tamper_proof_state {
  static_assert(std::is_enum_v<EnumT> || std::is_same_v<EnumT, bool>, "Must be enum or bool");

  uint32_t val_mask1_;
  uint32_t val_mask2_;

public:
  // Must explicitly initialize to avoid failing validation on first get()
  tamper_proof_state() noexcept { set(EnumT{}); }

  // Allows direct initialization: tamper_proof_state<MyEnum> state = MyEnum::Start;
  tamper_proof_state(EnumT initial_state) noexcept { set(initial_state); }

  void set(EnumT state) noexcept {
    uint32_t raw = static_cast<uint32_t>(state);
    val_mask1_ = raw ^ static_cast<uint32_t>(security::os_integer_region_cookie::integer_region_cookie1());
    val_mask2_ = ~raw ^ static_cast<uint32_t>(security::os_integer_region_cookie::integer_region_cookie2());
  }

  [[nodiscard]] EnumT get() const noexcept {
    uint32_t raw1 = val_mask1_ ^ static_cast<uint32_t>(security::os_integer_region_cookie::integer_region_cookie1());
    uint32_t raw2 = ~(val_mask2_ ^ static_cast<uint32_t>(security::os_integer_region_cookie::integer_region_cookie2()));

    // SECURITY: If an attacker flips a bit in mask1, they must flip the
    // EXACT corresponding bit in mask2, which is scattered by the cookie.
    if (raw1 != raw2) {
      RELOCO_TRAP();
    }
    return static_cast<EnumT>(raw1);
  }

  // Convenience operators
  tamper_proof_state &operator=(EnumT state) noexcept {
    set(state);
    return *this;
  }

  [[nodiscard]] operator EnumT() const noexcept { return get(); }
};

/** @brief Internal dual-mask implementation for tamper-protected booleans. */
template <typename Dummy = void> class tamper_bool_impl {
  uint32_t val_mask1_;
  uint32_t val_mask2_;

  // Wide, branchless representation
  static constexpr uint32_t TRUE_VAL = 0xFFFFFFFF;
  static constexpr uint32_t FALSE_VAL = 0x00000000;

  // Private tag for constructing directly from a raw 32-bit result
  /** @brief Private tag selecting construction from a validated raw value. */
  struct from_raw_tag {};

  inline tamper_bool_impl(uint32_t raw, from_raw_tag) noexcept {
    val_mask1_ = raw ^ static_cast<uint32_t>(security::os_integer_region_cookie::integer_region_cookie1());
    val_mask2_ = ~raw ^ static_cast<uint32_t>(security::os_integer_region_cookie::integer_region_cookie2());
  }

  [[nodiscard]] inline uint32_t get_raw() const noexcept {
    uint32_t raw1 = val_mask1_ ^ static_cast<uint32_t>(security::os_integer_region_cookie::integer_region_cookie1());
    uint32_t raw2 = ~(val_mask2_ ^ static_cast<uint32_t>(security::os_integer_region_cookie::integer_region_cookie2()));

    // SECURITY: Ensures dual-mask integrity AND enforces exact boolean boundaries.
    // Any partial bit-flip glitching will result in a value that isn't exactly
    // all-1s or all-0s, instantly trapping the CPU.
    if (raw1 != raw2 || (raw1 != TRUE_VAL && raw1 != FALSE_VAL)) {
      RELOCO_TRAP();
    }
    return raw1;
  }

public:
  tamper_bool_impl() noexcept : tamper_bool_impl(FALSE_VAL, from_raw_tag{}) {}

  tamper_bool_impl(bool b) noexcept : tamper_bool_impl(b ? TRUE_VAL : FALSE_VAL, from_raw_tag{}) {}

  // ---- Transparent Native Interaction ----

  tamper_bool_impl &operator=(bool b) noexcept {
    *this = tamper_bool_impl(b);
    return *this;
  }

  // Implicit conversion to bool allows natural `if (a)` or `if (a && b)`
  [[nodiscard]] operator bool() const noexcept { return get_raw() == TRUE_VAL; }

  // ---- Blind Bitwise Combinations (No Branches) ----
  // These evaluate securely in CPU registers using pure logic gates.

  [[nodiscard]] tamper_bool_impl operator&(const tamper_bool_impl &rhs) const noexcept {
    return {get_raw() & rhs.get_raw(), from_raw_tag{}};
  }

  [[nodiscard]] tamper_bool_impl operator|(const tamper_bool_impl &rhs) const noexcept {
    return {get_raw() | rhs.get_raw(), from_raw_tag{}};
  }

  [[nodiscard]] tamper_bool_impl operator^(const tamper_bool_impl &rhs) const noexcept {
    return {get_raw() ^ rhs.get_raw(), from_raw_tag{}};
  }

  // Logical NOT translates to Bitwise NOT flawlessly on 0xFFFFFFFF / 0x00000000
  [[nodiscard]] tamper_bool_impl operator!() const noexcept { return {~get_raw(), from_raw_tag{}}; }

  [[nodiscard]] tamper_bool_impl operator~() const noexcept { return {~get_raw(), from_raw_tag{}}; }

  // ---- Compound Operators ----

  tamper_bool_impl &operator&=(const tamper_bool_impl &rhs) noexcept {
    *this = *this & rhs;
    return *this;
  }

  tamper_bool_impl &operator|=(const tamper_bool_impl &rhs) noexcept {
    *this = *this | rhs;
    return *this;
  }

  tamper_bool_impl &operator^=(const tamper_bool_impl &rhs) noexcept {
    *this = *this ^ rhs;
    return *this;
  }
};

using tamper_bool = tamper_bool_impl<void>;

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
