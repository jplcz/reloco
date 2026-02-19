#pragma once
#include <reloco/allocator.hpp>
#include <reloco/concepts.hpp>
#include <reloco/core.hpp>

namespace reloco {

namespace detail {

template <typename T> struct sp_control_block {
  std::atomic<std::size_t> shared_count_{1};
  std::atomic<std::size_t> weak_count_{1};
  T *ptr_;

  sp_control_block(T *p) : ptr_(p) {}

  virtual ~sp_control_block() = default;

  virtual void delete_ptr() = 0;

  virtual void delete_control_block() = 0;

  void release_shared() noexcept {
    if (shared_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete_ptr();
      release_weak();
    }
  }

  void release_weak() noexcept {
    if (weak_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete_control_block();
    }
  }
};

template <typename T, typename Alloc>
struct sp_control_block_no_dp : sp_control_block<T> {
  Alloc *alloc_;

  sp_control_block_no_dp(T *p, Alloc *a) : sp_control_block<T>(p), alloc_(a) {}
  void delete_ptr() override {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      this->ptr_->~T();
    }
  }

  void delete_control_block() override {
    this->alloc_->deallocate(this, sizeof(*this));
  }
};

template <typename T, typename Alloc>
struct sp_combined_block : sp_control_block<T> {
  Alloc *alloc_;
  alignas(T) std::byte storage_[sizeof(T)];

  // ReSharper disable once CppUninitializedDependentBaseClass
  explicit sp_combined_block(Alloc *a)
      : sp_control_block<T>(nullptr), alloc_(a) {
    this->ptr_ = reinterpret_cast<T *>(storage_);
  }

  void delete_ptr() override {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      this->ptr_->~T();
    }
  }

  void delete_control_block() override {
    Alloc *a = this->alloc_;
    this->~sp_combined_block();
    a->deallocate(this, sizeof(sp_combined_block));
  }
};

struct enable_shared_from_this_base {};

} // namespace detail

template <typename T> class shared_ptr {
  detail::sp_control_block<T> *block_ = nullptr;

  explicit shared_ptr(detail::sp_control_block<T> *cb) noexcept : block_(cb) {}

  template <typename U> friend class weak_ptr;

public:
  shared_ptr() noexcept = default;

  ~shared_ptr() {
    if (block_)
      block_->release_shared();
  }

  shared_ptr(const shared_ptr &other) noexcept : block_(other.block_) {
    if (block_)
      block_->shared_count_.fetch_add(1, std::memory_order_relaxed);
  }

  constexpr shared_ptr(shared_ptr &&other) noexcept : block_(other.block_) {
    other.block_ = nullptr;
  }

  shared_ptr &operator=(const shared_ptr &other) noexcept {
    if (this != &other) {
      if (block_)
        block_->release_shared();
      block_ = other.block_;
      if (block_)
        block_->shared_count_.fetch_add(1, std::memory_order_relaxed);
    }
    return *this;
  }

  shared_ptr &operator=(shared_ptr &&other) noexcept {
    if (this != &other) {
      if (block_)
        block_->release_shared();
      block_ = other.block_;
      other.block_ = nullptr;
    }
    return *this;
  }

  T *operator->() const noexcept { return block_->ptr_; }
  T &operator*() const noexcept { return *(block_->ptr_); }
  T *get() const noexcept { return block_ ? block_->ptr_ : nullptr; }
  explicit operator bool() const noexcept { return block_ != nullptr; }

  std::size_t use_count() const noexcept {
    return block_ ? block_->shared_count_.load(std::memory_order_relaxed) : 0;
  }

  void reset() noexcept {
    if (block_) {
      block_->release_shared();
      block_ = nullptr;
    }
  }

  template <typename Tp, typename AllocP, typename... Args>
  friend result<shared_ptr<Tp>> try_allocate_shared(AllocP &alloc,
                                                    Args &&...args) noexcept;

  template <typename... Args>
  [[nodiscard]] friend result<shared_ptr>
  try_make_shared(Args &&...args) noexcept {
    return try_allocate_shared(get_default_allocator(),
                               std::forward<Args>(args)...);
  }

  template <typename Tp, typename Alloc, typename... Args>
  friend result<shared_ptr<Tp>>
  try_allocate_combined_shared(Alloc &alloc, Args &&...args) noexcept;

  template <typename... Args>
  [[nodiscard]] friend result<shared_ptr>
  try_make_combined_shared(Args &&...args) noexcept {
    return try_allocate_combined_shared(get_default_allocator(),
                                        std::forward<Args>(args)...);
  }
};

template <typename T>
bool operator==(const shared_ptr<T> &lhs, std::nullptr_t) noexcept {
  return lhs.get() == nullptr;
}

template <typename T>
bool operator!=(const shared_ptr<T> &lhs, std::nullptr_t) noexcept {
  return !(lhs == nullptr);
}

template <typename T, typename U>
bool operator==(const shared_ptr<T> &lhs, const shared_ptr<U> &rhs) noexcept {
  return lhs.get() == rhs.get();
}

template <typename T, typename U>
bool operator!=(const shared_ptr<T> &lhs, const shared_ptr<U> &rhs) noexcept {
  return lhs.get() != rhs.get();
}

template <typename T, typename U>
bool operator<(const shared_ptr<T> &lhs, const shared_ptr<U> &rhs) noexcept {
  return lhs.get() < rhs.get();
}

template <typename T> class weak_ptr {
  detail::sp_control_block<T> *block_ = nullptr;

public:
  template <typename Tp, typename Alloc, typename... Args>
  friend result<shared_ptr<Tp>> try_allocate_shared(Alloc &alloc,
                                                    Args &&...args) noexcept;

  template <typename Tp, typename Alloc, typename... Args>
  friend result<shared_ptr<Tp>>
  try_allocate_combined_shared(Alloc &alloc, Args &&...args) noexcept;

  constexpr weak_ptr() noexcept = default;

  weak_ptr(const shared_ptr<T> &other) noexcept : block_(other.block_) {
    if (block_) {
      block_->weak_count_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  ~weak_ptr() {
    if (block_)
      block_->release_weak();
  }

  weak_ptr(const weak_ptr &other) noexcept : block_(other.block_) {
    if (block_)
      block_->weak_count_.fetch_add(1, std::memory_order_relaxed);
  }

  weak_ptr(weak_ptr &&other) noexcept : block_(other.block_) {
    other.block_ = nullptr;
  }

  weak_ptr &operator=(const weak_ptr &other) noexcept {
    if (this != &other) {
      if (block_)
        block_->release_weak();
      block_ = other.block_;
      if (block_)
        block_->weak_count_.fetch_add(1, std::memory_order_relaxed);
    }
    return *this;
  }

  weak_ptr &operator=(weak_ptr &&other) noexcept {
    if (this != &other) {
      if (block_)
        block_->release_weak();
      block_ = other.block_;
      other.block_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] bool expired() const noexcept {
    return !block_ ||
           block_->shared_count_.load(std::memory_order_relaxed) == 0;
  }

  [[nodiscard]] result<shared_ptr<T>> lock() const noexcept {
    if (!block_)
      return std::unexpected(error::empty_pointer);

    auto count = block_->shared_count_.load(std::memory_order_relaxed);
    while (count != 0) {
      if (block_->shared_count_.compare_exchange_weak(
              count, count + 1, std::memory_order_acq_rel,
              std::memory_order_relaxed)) {
        return shared_ptr<T>(block_);
      }
    }
    return std::unexpected(error::pointer_expired);
  }
};

template <typename T>
class enable_shared_from_this : detail::enable_shared_from_this_base {
  mutable weak_ptr<T> weak_this_;

public:
  [[nodiscard]] result<shared_ptr<T>> shared_from_this() const noexcept {
    return weak_this_.lock();
  }

  template <typename Tp, typename Alloc, typename... Args>
  friend result<shared_ptr<Tp>> try_allocate_shared(Alloc &alloc,
                                                    Args &&...args) noexcept;

  template <typename Tp, typename Alloc, typename... Args>
  friend result<shared_ptr<Tp>>
  try_allocate_combined_shared(Alloc &alloc, Args &&...args) noexcept;

protected:
  constexpr enable_shared_from_this() noexcept = default;
};

template <typename T, typename Alloc, typename... Args>
[[nodiscard]] result<shared_ptr<T>>
try_allocate_shared(Alloc &alloc, Args &&...args) noexcept {
  auto block_t = alloc.allocate(sizeof(T), alignof(T));
  if (!block_t)
    return std::unexpected(block_t.error());

  T *raw_ptr = nullptr;
  if constexpr (has_try_create<T, Args...>) {
    auto res = T::try_create(std::forward<Args>(args)...);
    if (!res) {
      alloc.deallocate(block_t->ptr, sizeof(T));
      return std::unexpected(res.error());
    }
    raw_ptr = new (block_t->ptr) T(std::move(*res));
  } else {
    raw_ptr = new (block_t->ptr) T(std::forward<Args>(args)...);
  }

  auto block_cb =
      alloc.allocate(sizeof(detail::sp_control_block_no_dp<T, Alloc>),
                     alignof(detail::sp_control_block_no_dp<T, Alloc>));
  if (!block_cb) {
    raw_ptr->~T();
    alloc.deallocate(block_t->ptr, sizeof(T));
    return std::unexpected(block_cb.error());
  }

  auto *cb = new (block_cb->ptr)
      detail::sp_control_block_no_dp<T, Alloc>(raw_ptr, &alloc);

  if constexpr (std::is_base_of_v<detail::enable_shared_from_this_base, T>) {
    raw_ptr->weak_this_.block_ = cb;
    cb->weak_count_.fetch_add(1, std::memory_order_relaxed);
  }

  return shared_ptr(cb);
}

template <typename T, typename Alloc, typename... Args>
result<shared_ptr<T>> try_allocate_combined_shared(Alloc &alloc,
                                                   Args &&...args) noexcept {
  using CombinedType = detail::sp_combined_block<T, Alloc>;

  auto block_t = alloc.allocate(sizeof(CombinedType), alignof(CombinedType));
  if (!block_t)
    return std::unexpected(block_t.error());

  CombinedType *combined = static_cast<CombinedType *>(block_t->ptr);

  new (combined) CombinedType(&alloc);

  if constexpr (has_try_create<T, Args...>) {
    auto res = T::try_create(std::forward<Args>(args)...);
    if (!res) {
      alloc.deallocate(combined, sizeof(CombinedType));
      return std::unexpected(res.error());
    }
    new (combined->ptr_) T(std::move(*res));
  } else {
    new (combined->ptr_) T(std::forward<Args>(args)...);
  }

  if constexpr (std::is_base_of_v<detail::enable_shared_from_this_base, T>) {
    combined->ptr_->weak_this_.block_ = combined;
    combined->weak_count_.fetch_add(1, std::memory_order_relaxed);
  }

  return shared_ptr(combined);
}

} // namespace reloco