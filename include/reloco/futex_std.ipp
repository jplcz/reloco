// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Portable fallback implementation of reloco/futex.hpp's futex_wait/
// futex_wake_one/futex_wake_all, used whenever neither
// RELOCO_FUTEX_BACKEND_LINUX nor RELOCO_FUTEX_BACKEND_FREEBSD is
// selected: a small fixed "parking lot" of buckets (mutex.hpp's
// mutex/condition_variable), selected by hashing &word, exactly like
// Rust std's own generic futex fallback for targets without a real
// kernel futex. Included only from futex.hpp itself, guarded on
// RELOCO_SHARED_PROVIDE_DEFINITIONS -- never include this file directly.
//
// Multiple distinct futex_word objects can land in the same bucket
// (a hash collision); that only ever costs extra, harmless wakeups
// (every waiter in the bucket re-checks its own word/expected value
// after being woken, and futex_wait's contract already requires
// tolerating spurious wakeups), never a missed one -- a wake always
// notifies the whole bucket's condition_variable, and a wait always
// re-checks the word while still holding that same bucket's mutex.

namespace reloco {

namespace detail {

// Small, fixed, prime-ish bucket count: large enough that unrelated
// futex_word objects rarely collide, small enough to keep this table's
// footprint (mutex + condition_variable per bucket) modest.
inline constexpr std::size_t futex_parking_lot_bucket_count = 61;

struct futex_parking_lot_bucket {
  mutex guard;
  condition_variable cv;
};

class futex_parking_lot {
public:
  [[nodiscard]] futex_parking_lot_bucket &bucket_for(const futex_word &word) noexcept {
    auto addr = reinterpret_cast<std::uintptr_t>(&word);
    // Discard the low bits: alignof(futex_word) worth of them are always
    // zero and would otherwise waste hash entropy across buckets.
    addr /= alignof(futex_word);
    return buckets_[addr % futex_parking_lot_bucket_count];
  }

private:
  futex_parking_lot_bucket buckets_[futex_parking_lot_bucket_count];
};

[[nodiscard]] inline futex_parking_lot &global_parking_lot() noexcept {
  static futex_parking_lot lot;
  return lot;
}

} // namespace detail

RELOCO_API void futex_wait(const futex_word &word, std::uint32_t expected) noexcept {
  auto &bucket = detail::global_parking_lot().bucket_for(word);
  std::unique_lock<mutex> lock(bucket.guard);
  if (word.load(std::memory_order_acquire) != expected)
    return;
  auto wait_result = bucket.cv.wait(lock);
  RELOCO_ASSERT(static_cast<bool>(wait_result), "futex_wait: condition_variable::wait failed");
}

RELOCO_API void futex_wake_one(futex_word &word) noexcept {
  auto &bucket = detail::global_parking_lot().bucket_for(word);
  std::unique_lock<mutex> lock(bucket.guard);
  bucket.cv.notify_one();
}

RELOCO_API void futex_wake_all(futex_word &word) noexcept {
  auto &bucket = detail::global_parking_lot().bucket_for(word);
  std::unique_lock<mutex> lock(bucket.guard);
  bucket.cv.notify_all();
}

} // namespace reloco
