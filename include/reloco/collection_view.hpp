#pragma once
#include <reloco/collection_concepts.hpp>
#include <reloco/core.hpp>
#include <reloco/ownership_policy.hpp>

namespace reloco {

template <typename Container, template <typename> typename Policy>
  requires is_fallible_collection_view<Container,
                                       typename Container::value_type>
class collection_view {
public:
  using value_type = typename Container::value_type;
  using storage_type = typename Policy<Container>::storage_type;
  using PolicyType = Policy<Container>;
  using size_type = std::size_t;

  explicit collection_view(storage_type s) : m_storage(std::move(s)) {}

  size_t size() const noexcept {
    return Policy<Container>::get(m_storage).size();
  }

  bool empty() const noexcept {
    return Policy<Container>::get(m_storage).empty();
  }

  result<std::reference_wrapper<const value_type>>
  try_at(size_t i) const noexcept {
    return Policy<Container>::get(m_storage).try_at(i);
  }

  result<const value_type *> try_data() const noexcept {
    return Policy<Container>::get(m_storage).try_data();
  }

  const value_type &at(size_t i) const noexcept {
    return Policy<Container>::get(m_storage).at(i);
  }

  const value_type *data() const noexcept {
    return Policy<Container>::get(m_storage).data();
  }

  const value_type &unsafe_at(size_t i) const noexcept {
    return Policy<Container>::get(m_storage).unsafe_at(i);
  }

  const value_type *unsafe_data() const noexcept {
    return Policy<Container>::get(m_storage).unsafe_data();
  }

  template <typename C = Container>
    requires has_try_clone<C>
  auto try_clone() const
      -> result<collection_view<Container, policy::move_owner>> {
    const auto &container = Policy<Container>::get(m_storage);
    auto cloned_container_res = container.try_clone();
    if (!cloned_container_res) {
      return unexpected(cloned_container_res.error());
    }
    return collection_view<Container, policy::move_owner>(
        std::move(*cloned_container_res));
  }

protected:
  storage_type m_storage;
};

template <typename Container, template <typename> typename Policy>
  requires is_mutable_fallible_collection_view<Container,
                                               typename Container::value_type>
class mutable_collection_view : public collection_view<Container, Policy> {
public:
  using Base = collection_view<Container, Policy>;
  using PolicyType = typename Base::PolicyType;
  using value_type = typename Base::value_type;

  using Base::Base;

  result<std::reference_wrapper<value_type>> try_at(size_t i) noexcept {
    return PolicyType::get(this->m_storage).try_at(i);
  }

  result<value_type *> try_data() noexcept {
    return PolicyType::get(this->m_storage).try_data();
  }

  value_type &at(size_t i) noexcept {
    return PolicyType::get(this->m_storage).at(i);
  }

  value_type *data() noexcept {
    return PolicyType::get(this->m_storage).data();
  }

  value_type &unsafe_at(size_t i) noexcept {
    return PolicyType::get(this->m_storage).unsafe_at(i);
  }

  value_type *unsafe_data() noexcept {
    return PolicyType::get(this->m_storage).unsafe_data();
  }

  result<value_type *> try_push_back(value_type &&item) {
    return PolicyType::get(this->m_storage).try_push_back(std::move(item));
  }

  result<void> try_reserve(size_t capacity) {
    return PolicyType::get(this->m_storage).try_reserve(capacity);
  }

  result<void> try_erase(size_t i) {
    return PolicyType::get(this->m_storage).try_erase(i);
  }

  void clear() noexcept { PolicyType::get(this->m_storage).clear(); }

  using Base::at;
  using Base::data;
  using Base::try_at;
  using Base::try_data;
  using Base::unsafe_at;
  using Base::unsafe_data;
};

template <typename Element> struct any_view_vtable {
  size_t (*size)(const void *) noexcept;
  result<std::reference_wrapper<const Element>> (*try_at)(const void *,
                                                          size_t) noexcept;
  void (*destroy)(void *, fallible_allocator &alloc) noexcept;
  result<void *> (*try_clone)(const void *, fallible_allocator &alloc) noexcept;
};

namespace detail {

template <typename Container, template <typename> typename Policy,
          typename Element>
struct any_view_vtable_factory {
  using ViewType = collection_view<Container, Policy>;

  static constexpr any_view_vtable<Element> instance = {
      .size = [](const void *ctx) noexcept -> size_t {
        return static_cast<const ViewType *>(ctx)->size();
      },

      .try_at = [](const void *ctx, size_t i) noexcept
          -> result<std::reference_wrapper<const Element>> {
        return static_cast<const ViewType *>(ctx)->try_at(i);
      },

      .destroy =
          [](void *ctx, fallible_allocator &alloc) noexcept {
            static_cast<ViewType *>(ctx)->~ViewType();
            alloc.deallocate(ctx, sizeof(ViewType));
          },

      .try_clone = [](const void *ctx,
                      fallible_allocator &alloc) noexcept -> result<void *> {
        if constexpr (has_try_clone<ViewType>) {
          auto res = alloc.allocate(sizeof(ViewType), alignof(ViewType));
          if (!res)
            return unexpected(error::allocation_failed);
          auto cloned = static_cast<const ViewType *>(ctx)->try_clone();
          if (!cloned.has_value()) {
            alloc.deallocate(res->ptr, res->size);
            return unexpected(cloned.error());
          }
          return new (res->ptr) ViewType(std::move(cloned.value()));
        } else {
          return unexpected(error::unsupported_operation);
        }
      }};
};

} // namespace detail

template <typename Element> class any_view {
  void *m_ctx;
  const any_view_vtable<Element> *m_vtable;
  fallible_allocator *m_alloc;

  any_view(void *ctx, const any_view_vtable<Element> *vt,
           fallible_allocator *alloc)
      : m_ctx(ctx), m_vtable(vt), m_alloc(alloc) {}

public:
  template <typename Container, template <typename> typename Policy>
    requires is_fallible_collection_view<Container, Element>
  static result<any_view> try_create(collection_view<Container, Policy> &&view,
                                     fallible_allocator &alloc) {
    using ViewType = collection_view<Container, Policy>;

    // Fallible allocation of the erased context
    auto ctx_res = alloc.allocate(sizeof(ViewType), alignof(ViewType));
    if (!ctx_res)
      return unexpected(error::allocation_failed);

    new (ctx_res->ptr) ViewType(std::move(view));

    return any_view(
        ctx_res->ptr,
        &detail::any_view_vtable_factory<Container, Policy, Element>::instance,
        &alloc);
  }

  ~any_view() {
    if (m_ctx)
      m_vtable->destroy(m_ctx, *m_alloc);
  }

  result<any_view> try_clone() const {
    if (!m_ctx || !m_vtable)
      return unexpected(error::not_initialized);

    auto res = m_vtable->try_clone(m_ctx, *m_alloc);
    if (!res)
      return unexpected(res.error());

    return any_view(*res, m_vtable, m_alloc);
  }

  any_view(any_view &&other) noexcept
      : m_ctx(other.m_ctx), m_vtable(other.m_vtable), m_alloc(other.m_alloc) {
    other.m_ctx = nullptr;
    other.m_vtable = nullptr;
  }

  any_view &operator=(any_view &&other) noexcept {
    if (this != &other) {
      if (m_ctx)
        m_vtable->destroy(m_ctx, *m_alloc);
      m_ctx = other.m_ctx;
      m_vtable = other.m_vtable;
      m_alloc = other.m_alloc;
      other.m_ctx = nullptr;
      other.m_vtable = nullptr;
    }
    return *this;
  }

  any_view(const any_view &) = delete;
  any_view &operator=(const any_view &) = delete;

  size_t size() const noexcept { return m_ctx ? m_vtable->size(m_ctx) : 0; }
  bool empty() const noexcept { return size() == 0; }

  result<std::reference_wrapper<const Element>>
  try_at(size_t i) const noexcept {
    if (!m_ctx)
      return unexpected(error::not_initialized);
    return m_vtable->try_at(m_ctx, i);
  }

  const Element &at(size_t i) noexcept {
    const auto result = try_at(i);
    RELOCO_ASSERT(result.has_value(), "Could not obtain element reference");
    return *result;
  }
};

} // namespace reloco