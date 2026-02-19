#pragma once
#include <reloco/allocator.hpp>
#include <reloco/concepts.hpp>
#include <reloco/core.hpp>

namespace reloco {

namespace detail {

struct sp_control_block {
  std::atomic<std::size_t> shared_count_{1};
  std::atomic<std::size_t> weak_count_{1};
  void *ptr_;

  explicit sp_control_block(void *p) : ptr_(p) {}

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
struct sp_control_block_no_dp : sp_control_block {
  Alloc *alloc_;

  sp_control_block_no_dp(T *p, Alloc *a) : sp_control_block(p), alloc_(a) {}
  void delete_ptr() override {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      static_cast<T *>(this->ptr_)->~T();
    }
  }

  void delete_control_block() override {
    this->alloc_->deallocate(this, sizeof(*this));
  }
};

template <typename T, typename Alloc>
struct sp_combined_block : sp_control_block {
  Alloc *alloc_;
  alignas(T) std::byte storage_[sizeof(T)];

  // ReSharper disable once CppUninitializedDependentBaseClass
  explicit sp_combined_block(Alloc *a) : sp_control_block(nullptr), alloc_(a) {
    this->ptr_ = reinterpret_cast<T *>(storage_);
  }

  void delete_ptr() override {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      static_cast<T *>(this->ptr_)->~T();
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
  detail::sp_control_block *block_ = nullptr;
  T *ptr_ = nullptr;

  explicit shared_ptr(detail::sp_control_block *cb, T *p) noexcept
      : block_(cb), ptr_(p) {}

  template <typename U> friend class weak_ptr;
  template <typename U> friend class shared_ptr;

public:
  shared_ptr() noexcept = default;

  ~shared_ptr() {
    if (block_)
      block_->release_shared();
  }

  shared_ptr(const shared_ptr &other) noexcept
      : block_(other.block_), ptr_(other.ptr_) {
    if (block_)
      block_->shared_count_.fetch_add(1, std::memory_order_relaxed);
  }

  template <typename U>
  shared_ptr(const shared_ptr<U> &other, T *ptr) noexcept
      : block_(other.block_), ptr_(ptr) {
    if (block_)
      block_->shared_count_.fetch_add(1, std::memory_order_relaxed);
  }

  template <typename U>
  shared_ptr(const shared_ptr<U> &other) noexcept
      : block_(other.block_), ptr_(other.ptr_) {
    if (block_)
      block_->shared_count_.fetch_add(1, std::memory_order_relaxed);
  }

  constexpr shared_ptr(shared_ptr &&other) noexcept
      : block_(other.block_), ptr_(other.ptr_) {
    other.block_ = nullptr;
    other.ptr_ = nullptr;
  }

  shared_ptr &operator=(const shared_ptr &other) noexcept {
    if (this != &other) {
      if (block_)
        block_->release_shared();
      block_ = other.block_;
      ptr_ = other.ptr_;
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
      ptr_ = other.ptr_;
      other.ptr_ = nullptr;
    }
    return *this;
  }

  T *operator->() const noexcept { return ptr_; }
  T &operator*() const noexcept { return *(ptr_); }
  T *get() const noexcept { return ptr_; }
  explicit operator bool() const noexcept { return ptr_ != nullptr; }

  std::size_t use_count() const noexcept {
    return block_ ? block_->shared_count_.load(std::memory_order_relaxed) : 0;
  }

  void reset() noexcept {
    if (block_) {
      block_->release_shared();
      block_ = nullptr;
      ptr_ = nullptr;
    }
  }

  template <typename Tp, typename AllocP, typename... Args>
  friend result<shared_ptr<Tp>> try_allocate_shared(AllocP &alloc,
                                                    Args &&...args) noexcept;

  template <typename Tp, typename... Args>
  friend result<shared_ptr<Tp>> try_make_shared(Args &&...args) noexcept;

  template <typename Tp, typename Alloc, typename... Args>
  friend result<shared_ptr<Tp>>
  try_allocate_combined_shared(Alloc &alloc, Args &&...args) noexcept;

  template <typename Tp, typename... Args>
  friend result<shared_ptr<Tp>>
  try_make_combined_shared(Args &&...args) noexcept;
};

template <typename T, typename U>
auto operator<=>(const shared_ptr<T> &a, const shared_ptr<U> &b) noexcept {
  return a.get() <=> b.get();
}

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
  detail::sp_control_block *block_ = nullptr;
  T *ptr_ = nullptr;

public:
  template <typename Tp, typename Alloc, typename... Args>
  friend result<shared_ptr<Tp>> try_allocate_shared(Alloc &alloc,
                                                    Args &&...args) noexcept;

  template <typename Tp, typename Alloc, typename... Args>
  friend result<shared_ptr<Tp>>
  try_allocate_combined_shared(Alloc &alloc, Args &&...args) noexcept;

  constexpr weak_ptr() noexcept = default;

  weak_ptr(const shared_ptr<T> &other) noexcept
      : block_(other.block_), ptr_(other.ptr_) {
    if (block_) {
      block_->weak_count_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  ~weak_ptr() {
    if (block_)
      block_->release_weak();
  }

  weak_ptr(const weak_ptr &other) noexcept
      : block_(other.block_), ptr_(other.ptr_) {
    if (block_)
      block_->weak_count_.fetch_add(1, std::memory_order_relaxed);
  }

  weak_ptr(weak_ptr &&other) noexcept : block_(other.block_), ptr_(other.ptr_) {
    other.block_ = nullptr;
    other.ptr_ = nullptr;
  }

  weak_ptr &operator=(const weak_ptr &other) noexcept {
    if (this != &other) {
      if (block_)
        block_->release_weak();
      block_ = other.block_;
      ptr_ = other.ptr_;
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
      ptr_ = other.ptr_;
      other.ptr_ = nullptr;
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
        return shared_ptr<T>(block_, ptr_);
      }
    }
    return std::unexpected(error::pointer_expired);
  }

  template <typename U>
  bool owner_before(const weak_ptr<U> &other) const noexcept {
    return block_ < other.block_;
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
    raw_ptr->weak_this_.ptr_ = raw_ptr;
    cb->weak_count_.fetch_add(1, std::memory_order_relaxed);
  }

  return shared_ptr<T>(cb, raw_ptr);
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

  T *raw_ptr = static_cast<T *>(combined->ptr_);

  if constexpr (has_try_create<T, Args...>) {
    auto res = T::try_create(std::forward<Args>(args)...);
    if (!res) {
      alloc.deallocate(combined, sizeof(CombinedType));
      return std::unexpected(res.error());
    }
    new (raw_ptr) T(std::move(*res));
  } else {
    new (raw_ptr) T(std::forward<Args>(args)...);
  }

  if constexpr (std::is_base_of_v<detail::enable_shared_from_this_base, T>) {
    raw_ptr->weak_this_.block_ = combined;
    raw_ptr->weak_this_.ptr_ = raw_ptr;
    combined->weak_count_.fetch_add(1, std::memory_order_relaxed);
  }

  return shared_ptr<T>(combined, raw_ptr);
}

template <typename Tp, typename... Args>
[[nodiscard]] result<shared_ptr<Tp>>
try_make_combined_shared(Args &&...args) noexcept {
  return try_allocate_combined_shared<Tp>(get_default_allocator(),
                                          std::forward<Args>(args)...);
}

template <typename Tp, typename... Args>
[[nodiscard]] result<shared_ptr<Tp>> try_make_shared(Args &&...args) noexcept {
  return try_allocate_shared<Tp>(get_default_allocator(),
                                 std::forward<Args>(args)...);
}

template <typename T, typename U>
shared_ptr<T> static_pointer_cast(const shared_ptr<U> &r) noexcept {
  auto p = static_cast<T *>(r.get());
  return shared_ptr<T>(r, p);
}

template <typename T, typename U>
shared_ptr<T> dynamic_pointer_cast(const shared_ptr<U> &r) noexcept {
  if (auto *p = dynamic_cast<T *>(r.get())) {
    return shared_ptr<T>(r, p);
  }
  return shared_ptr<T>();
}

template <typename T, typename U>
shared_ptr<T> const_pointer_cast(const shared_ptr<U> &r) noexcept {
  return shared_ptr<T>(r, const_cast<T *>(r.get()));
}

} // namespace reloco

namespace std {

template <typename T> struct hash<reloco::shared_ptr<T>> {
  size_t operator()(const reloco::shared_ptr<T> &p) const noexcept {
    return hash<T *>{}(p.get());
  }
};

} // namespace std
