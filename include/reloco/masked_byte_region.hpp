// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file masked_byte_region.hpp
 * @brief `masked_byte_region<Size>`: a fixed-size byte array that keeps
 * every byte XOR-masked at rest, so the plaintext contents never exist
 * contiguously in memory -- only individual bytes are ever decoded, one
 * at a time, on read.
 *
 * Each instance derives its own 64-bit nonce from its own address
 * (`security::generate_weak_nonce`); combined with the process-wide
 * `security::byte_region_cookie()` and the byte offset, every byte gets
 * an independent one-time-pad-style mask (`security::get_byte_mask`), so
 * two instances holding identical plaintext never produce identical
 * ciphertext, and a single byte-region instance never repeats a mask
 * across offsets.
 *
 * ## Threat model
 *
 * This is **obfuscation-at-rest against passive memory scanning/heap
 * scraping (e.g. a coredump, a `/proc/<pid>/mem` scrape, an adjacent
 * buffer overflow read), not cryptographic secrecy**: both the
 * process-wide cookie and every instance's nonce live in the very same
 * address space as the data they protect, derived from ASLR-influenced
 * addresses rather than a hardware RNG (see
 * `security::generate_weak_nonce`/`security::detail::generate_weak_region_cookie`
 * in this file and `masked_pointer.hpp` respectively). An attacker who
 * can read arbitrary process memory more than once can always recover
 * the cookie, the nonce, and therefore the plaintext. What this class
 * *does* guarantee is that a single snapshot of memory (one coredump, one
 * screen-scraped debugger view, one overflow read of an adjacent buffer)
 * never yields the plaintext directly, and that `wipe()` (used on
 * destruction paths and cache eviction) leaves no recoverable plaintext
 * behind even under compiler dead-store elimination -- see `wipe()`
 * below.
 *
 * ## Rust-like operators and iterator support
 *
 * `masked_byte_region` is move-only-by-omission today (copy *and* move
 * are both deleted, matching the original design: duplicating either the
 * nonce or the plaintext bytes without a caller explicitly asking for it
 * is exactly the kind of accidental plaintext exposure this class exists
 * to prevent). `fill(uint8_t)` (Rust's `[T]::fill`) and `wipe()` (already
 * present) round out the "bulk" operations; `begin()`/`end()` (mutable,
 * yielding the `reference` proxy) and `cbegin()`/`cend()` (read-only,
 * yielding a plain `uint8_t` by value, exactly like
 * `std::vector<bool>::const_iterator`) provide full random-access
 * iterator support, so a `masked_byte_region<N>` composes with the usual
 * `<algorithm>` machinery (`std::copy`, `std::fill`, range-`for`, etc.)
 * exactly like any other byte container, without ever materializing the
 * whole plaintext contiguously the way `data()`-style raw-pointer access
 * would.
 */

#include "detail/assert.hpp"
#include "lifetime.hpp"
#include "once_lock.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {
namespace security {

namespace detail {

/**
 * @brief Weak, best-effort process-wide entropy source for
 * `byte_region_cookie()`. See `masked_pointer.hpp`'s identically-named
 * (and identically weak, for the identical reason) cookie source -- kept
 * as an independent copy here rather than a shared include so this
 * header remains a standalone, opt-in unit like the rest of reloco.
 */
[[nodiscard]] inline uint64_t generate_weak_region_cookie() noexcept {
  uint64_t stack_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&stack_addr));
  uint64_t code_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&generate_weak_region_cookie));
  uint64_t mix = stack_addr ^ code_addr;
  mix ^= (mix >> 30);
  mix *= 0xbf58476d1ce4e5b9ULL;
  mix ^= (mix >> 27);
  mix *= 0x94d049bb133111ebULL;
  mix ^= (mix >> 31);
  return mix;
}

} // namespace detail

inline constexpr uint64_t GOLDEN_RATIO_64 = 0x9e3779b97f4a7c15ULL;

#if !defined(RELOCO_KERNEL)
/**
 * @brief The process-wide secret cookie mixed into every
 * `masked_byte_region`'s per-byte mask. Lazily generated once via
 * `once_lock<uint64_t>` (never a function-local "magic" static -- see
 * `once_lock.hpp`) rather than a declared-but-never-defined `extern`
 * global, which would fail to link the moment any translation unit
 * actually used a `masked_byte_region`.
 */
struct os_byte_region_cookie {
  static inline once_lock<uint64_t> cookie{};
  [[nodiscard]] static inline uint64_t byte_region_cookie() noexcept {
    return cookie.get_or_init(detail::generate_weak_region_cookie);
  }
};
#else
struct os_byte_region_cookie {
  [[nodiscard]] static uint64_t byte_region_cookie() noexcept;
};
#endif

/**
 * @brief Computes the XOR mask for a specific byte offset on the fly.
 *
 * Mixes the process-wide secret cookie, a 64-bit per-instance nonce, and
 * the word offset to ensure identical bytes produce entirely different
 * ciphertexts.
 */
[[nodiscard]] inline uint8_t get_byte_mask(uint64_t nonce, size_t offset) noexcept {
  size_t word_index = offset / 8;
  size_t byte_in_word = offset % 8;

  // Mix the 64-bit inputs.
  uint64_t stream_key = os_byte_region_cookie::byte_region_cookie() ^ nonce ^ static_cast<uint64_t>(word_index);

  // Avalanche the key to distribute entropy across all bits.
  stream_key *= GOLDEN_RATIO_64;
  stream_key ^= (stream_key >> 32);

  return static_cast<uint8_t>((stream_key >> (byte_in_word * 8)) & 0xFF);
}

/**
 * @brief Generates a 64-bit spatial nonce based on the object's own
 * memory address. Uses a SplitMix64-style avalanche to thoroughly
 * scramble the address. See the file-level "Threat model" section for
 * exactly what this weak, address-derived nonce is (and is not) a
 * defense against.
 */
[[nodiscard]] inline uint64_t generate_weak_nonce(void *self_ptr) noexcept {
  uint64_t mix = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(self_ptr));
  mix ^= (mix >> 30);
  mix *= 0xbf58476d1ce4e5b9ULL;
  mix ^= (mix >> 27);
  mix *= 0x94d049bb133111ebULL;
  mix ^= (mix >> 31);
  return mix;
}

/**
 * @brief Default storage policy: Nonce lives inline with the object.
 * Fast and requires no allocation, but vulnerable to adjacent memory leaks.
 */
class inline_nonce_storage {
  uint64_t nonce_;

protected:
  inline_nonce_storage(void *obj_ptr) noexcept : nonce_(generate_weak_nonce(obj_ptr)) {}
  ~inline_nonce_storage() = default;

  [[nodiscard]] inline uint64_t get_nonce() const noexcept { return nonce_; }
};

} // namespace security

/**
 * @brief A fixed-size memory region that securely obfuscates bytes at
 * rest. See the file-level documentation above for this class's threat
 * model.
 *
 * Supports byte-level random access via proxy objects and full
 * random-access iterators, ensuring the full plaintext never exists
 * contiguously in memory.
 */
template <size_t Size, typename NoncePolicy = security::inline_nonce_storage> class masked_byte_region : NoncePolicy {
  static_assert(Size > 0, "Region size must be > 0");

  uint8_t obfuscated_data_[Size];

  template <bool IsConst> class byte_iterator;

public:
  using value_type = uint8_t;
  using size_type = size_t;
  using difference_type = std::ptrdiff_t;
  using iterator = byte_iterator<false>;
  using const_iterator = byte_iterator<true>;

  /**
   * @brief Proxy class to support array-like assignment: region[i] = 0xAA;
   */
  class reference {
    masked_byte_region &region_;
    size_t offset_;

    friend class masked_byte_region;
    reference(masked_byte_region &r, size_t o) noexcept : region_(r), offset_(o) {}

  public:
    // Implicit conversion for reading: uint8_t b = region[i];
    [[nodiscard]] operator uint8_t() const noexcept { return region_.read_byte(offset_); }

    // Assignment for writing: region[i] = 0xAA;
    reference &operator=(uint8_t val) noexcept {
      region_.write_byte(offset_, val);
      return *this;
    }

    reference &operator^=(uint8_t val) noexcept {
      region_.write_byte(offset_, region_.read_byte(offset_) ^ val);
      return *this;
    }
  };

  /**
   * @brief Constructs the region with a unique spatial nonce.
   */
  masked_byte_region() noexcept : NoncePolicy(this) { wipe(); }

  // Disable copy/move to prevent accidental plaintext exposure or nonce
  // duplication -- see the file-level "Rust-like operators" section.
  masked_byte_region(const masked_byte_region &) = delete;
  masked_byte_region &operator=(const masked_byte_region &) = delete;
  masked_byte_region(masked_byte_region &&) = delete;
  masked_byte_region &operator=(masked_byte_region &&) = delete;

  ~masked_byte_region() { wipe(); }

  // ---- Byte-Level Access API ----

  [[nodiscard]] uint8_t read_byte(size_t offset) const noexcept {
    RELOCO_ASSERT(offset < Size, "Index out of range");
    return obfuscated_data_[offset] ^ security::get_byte_mask(this->get_nonce(), offset);
  }

  void write_byte(size_t offset, uint8_t val) noexcept {
    RELOCO_ASSERT(offset < Size, "Index out of range");
    obfuscated_data_[offset] = val ^ security::get_byte_mask(this->get_nonce(), offset);
  }

  // ---- Operator Overloads ----

  [[nodiscard]] reference operator[](size_t offset) noexcept { return reference(*this, offset); }

  [[nodiscard]] uint8_t operator[](size_t offset) const noexcept { return read_byte(offset); }

  // ---- Utility ----

  [[nodiscard]] constexpr size_t size() const noexcept { return Size; }

  /**
   * @brief Sets every byte in the region to @p val, matching Rust's
   * `[T]::fill`.
   */
  void fill(uint8_t val) noexcept {
    for (size_t i = 0; i < Size; ++i)
      write_byte(i, val);
  }

  /**
   * @brief Securely wipes the region's contents from memory.
   *
   * Writes go through a `volatile uint8_t *` alias of the backing array
   * rather than a plain store, so every write is an observable side
   * effect the compiler is forbidden from eliding, reordering past, or
   * folding away -- portable to every compiler (unlike a GCC/Clang-only
   * `__asm__ __volatile__` memory-clobber barrier, which MSVC cannot
   * compile at all).
   */
  void wipe() noexcept {
    volatile uint8_t *raw = obfuscated_data_;
    for (size_t i = 0; i < Size; ++i) {
      raw[i] = static_cast<uint8_t>(0x00 ^ security::get_byte_mask(this->get_nonce(), i));
    }
  }

  // ---- Iterators ----

  [[nodiscard]] iterator begin() noexcept RELOCO_LIFETIMEBOUND { return iterator(this, 0); }
  [[nodiscard]] iterator end() noexcept RELOCO_LIFETIMEBOUND { return iterator(this, Size); }

  [[nodiscard]] const_iterator begin() const noexcept RELOCO_LIFETIMEBOUND { return const_iterator(this, 0); }
  [[nodiscard]] const_iterator end() const noexcept RELOCO_LIFETIMEBOUND { return const_iterator(this, Size); }

  [[nodiscard]] const_iterator cbegin() const noexcept RELOCO_LIFETIMEBOUND { return const_iterator(this, 0); }
  [[nodiscard]] const_iterator cend() const noexcept RELOCO_LIFETIMEBOUND { return const_iterator(this, Size); }

private:
  /**
   * @brief Random-access iterator over a `masked_byte_region<Size>`.
   * `IsConst == false` dereferences to the mutable `reference` proxy
   * (so `*it = 0xAA;` re-masks and stores through the iterator, exactly
   * like `operator[]`); `IsConst == true` dereferences to a plain
   * `uint8_t` by value -- there is no plaintext to take the address of,
   * exactly like `std::vector<bool>::const_iterator`, so neither
   * instantiation provides `operator->`.
   */
  template <bool IsConst> class byte_iterator {
  public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = uint8_t;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = std::conditional_t<IsConst, uint8_t, typename masked_byte_region::reference>;

  private:
    using RegionPtr = std::conditional_t<IsConst, const masked_byte_region *, masked_byte_region *>;
    RegionPtr region_{nullptr};
    size_t idx_{0};

    friend class masked_byte_region;
    byte_iterator(RegionPtr region, size_t idx) noexcept : region_(region), idx_(idx) {}

  public:
    byte_iterator() = default;

    [[nodiscard]] reference operator*() const noexcept {
      RELOCO_ASSERT(region_ != nullptr, "Dereferencing an uninitialized masked_byte_region iterator");
      RELOCO_ASSERT(idx_ < Size, "Out-of-bounds masked_byte_region iterator dereference (likely dereferenced end())");
      return (*region_)[idx_];
    }

    [[nodiscard]] reference operator[](difference_type n) const noexcept { return *(*this + n); }

    byte_iterator &operator++() noexcept {
      ++idx_;
      return *this;
    }

    byte_iterator operator++(int) noexcept {
      auto tmp = *this;
      ++(*this);
      return tmp;
    }

    byte_iterator &operator--() noexcept {
      --idx_;
      return *this;
    }

    byte_iterator operator--(int) noexcept {
      auto tmp = *this;
      --(*this);
      return tmp;
    }

    byte_iterator &operator+=(difference_type n) noexcept {
      if (n >= 0) {
        idx_ += static_cast<size_t>(n);
      } else {
        idx_ -= static_cast<size_t>(-n);
      }
      return *this;
    }

    byte_iterator &operator-=(difference_type n) noexcept { return *this += -n; }

    [[nodiscard]] friend byte_iterator operator+(byte_iterator it, difference_type n) noexcept { return it += n; }
    [[nodiscard]] friend byte_iterator operator+(difference_type n, byte_iterator it) noexcept { return it += n; }
    [[nodiscard]] friend byte_iterator operator-(byte_iterator it, difference_type n) noexcept { return it -= n; }

    [[nodiscard]] friend difference_type operator-(const byte_iterator &a, const byte_iterator &b) noexcept {
      return static_cast<difference_type>(a.idx_) - static_cast<difference_type>(b.idx_);
    }

    [[nodiscard]] bool operator==(const byte_iterator &other) const noexcept {
      return region_ == other.region_ && idx_ == other.idx_;
    }
    [[nodiscard]] bool operator!=(const byte_iterator &other) const noexcept { return !(*this == other); }
    [[nodiscard]] bool operator<(const byte_iterator &other) const noexcept { return idx_ < other.idx_; }
    [[nodiscard]] bool operator>(const byte_iterator &other) const noexcept { return idx_ > other.idx_; }
    [[nodiscard]] bool operator<=(const byte_iterator &other) const noexcept { return idx_ <= other.idx_; }
    [[nodiscard]] bool operator>=(const byte_iterator &other) const noexcept { return idx_ >= other.idx_; }
  };
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
