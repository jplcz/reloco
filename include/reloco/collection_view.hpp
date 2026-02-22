#pragma once
#include <reloco/collection_concepts.hpp>
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

} // namespace reloco