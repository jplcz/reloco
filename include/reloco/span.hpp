#pragma once
#include <reloco/assert.hpp>
#include <reloco/core.hpp>
#include <span>

namespace reloco {

template <typename T, std::size_t Extent = std::dynamic_extent> class span {
private:
  using base_t = std::span<T, Extent>;

  base_t span_;

public:
  using element_type = base_t::element_type;
  using value_type = base_t::value_type;
  using size_type = base_t::size_type;
  using difference_type = base_t::difference_type;
  using pointer = base_t::pointer;
#if __cplusplus > 202002L
  using const_pointer = base_t::const_iterator;
#endif
  using reference = base_t::reference;
  using const_reference = base_t::const_reference;
  using iterator = base_t::iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;
#if __cplusplus > 202002L
  using const_iterator = std::const_iterator<iterator>;
  using const_reverse_iterator = std::const_iterator<reverse_iterator>;
#endif

  static constexpr size_t extent = Extent;

  constexpr span() noexcept
    requires(Extent == 0 || Extent == std::dynamic_extent)
  = default;

  constexpr span(T *ptr, size_type count) noexcept
      : span_(ptr, ptr ? count : 0) {}

  [[nodiscard]] constexpr result<std::reference_wrapper<T>>
  try_at(size_type index) const noexcept {
    if (index >= span_.size()) [[unlikely]] {
      return unexpected(error::out_of_bounds);
    }
    return std::ref(span_[index]);
  }

  constexpr T &operator[](size_type index) const noexcept {
    RELOCO_ASSERT(index < span_.size() && "Span index out of bounds");
    return span_[index];
  }

  [[nodiscard]] constexpr T &unsafe_at(size_type index) const noexcept {
    RELOCO_DEBUG_ASSERT(index < span_.size() && "Span index out of bounds");
    return span_.data()[index];
  }

  [[nodiscard]] constexpr result<span<T>>
  try_subspan(size_type offset,
              size_type count = std::dynamic_extent) const noexcept {
    if (offset > span_.size())
      return reloco::unexpected(error::out_of_bounds);

    size_type actual_count =
        (count == std::dynamic_extent) ? (span_.size() - offset) : count;
    if (offset + actual_count > span_.size())
      return reloco::unexpected(error::out_of_bounds);

    return span<T>(span_.data() + offset, actual_count);
  }

  constexpr span<T> unsafe_subspan(size_type offset,
                                   size_type count) const noexcept {
    return span<T>(span_.data() + offset, count);
  }

  constexpr size_type size() const noexcept { return span_.size(); }
  constexpr T *unsafe_data() const noexcept {
    RELOCO_DEBUG_ASSERT(!span_.empty(), "span has no data");
    return span_.data();
  }
  constexpr bool empty() const noexcept { return span_.empty(); }

  [[nodiscard]] constexpr iterator begin() noexcept { return span_.begin(); }
  [[nodiscard]] constexpr iterator end() noexcept { return span_.end(); }
#if __cplusplus > 202002L
  [[nodiscard]] constexpr const_iterator begin() const noexcept {
    return span_.begin();
  }
  [[nodiscard]] constexpr const_iterator end() const noexcept {
    return span_.end();
  }
#endif
  [[nodiscard]] constexpr reverse_iterator rbegin() noexcept {
    return span_.rbegin();
  }
  [[nodiscard]] constexpr reverse_iterator rend() noexcept {
    return span_.rend();
  }
  [[nodiscard]] constexpr result<std::reference_wrapper<T>>
  try_front() const noexcept {
    if (empty()) [[unlikely]]
      return unexpected(error::out_of_bounds);
    return std::ref(span_.front());
  }

  [[nodiscard]] constexpr result<std::reference_wrapper<T>>
  try_back() const noexcept {
    if (empty()) [[unlikely]]
      return unexpected(error::out_of_bounds);
    return std::ref(span_.back());
  }

  constexpr T &front() const noexcept {
    RELOCO_ASSERT(!empty(), "front() called on empty span");
    return span_.front();
  }

  constexpr T &unsafe_front() const noexcept {
    RELOCO_DEBUG_ASSERT(!empty(), "front() called on empty span");
    return *span_.data();
  }

  [[nodiscard]] constexpr result<span<T>>
  try_first(size_type n) const noexcept {
    if (n > size())
      return unexpected(error::out_of_bounds);
    return span<T>(span_.data(), n);
  }

  [[nodiscard]] constexpr result<span<T>> try_last(size_type n) const noexcept {
    if (n > size())
      return unexpected(error::out_of_bounds);
    return span<T>(span_.data() + (size() - n), n);
  }

  constexpr span<T> unsafe_first(size_type n) const noexcept {
    return span<T>(span_.data(), n);
  }

  [[nodiscard]] constexpr span<const std::byte> as_bytes() const noexcept {
    return span<const std::byte>(
        reinterpret_cast<const std::byte *>(span_.data()), span_.size_bytes());
  }
};

} // namespace reloco