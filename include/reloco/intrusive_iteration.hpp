#pragma once
#include "iterator.hpp"

namespace reloco {

/**
 * @brief Transaction owning an extracted intrusive node until commit or rollback.
 * @tparam Container Intrusive container type.
 * @tparam T Node type.
 */
template <typename Container, typename T> class RELOCO_CONSUMABLE(unconsumed) isolated_node_tx {
public:
  // Takes ownership of the statically allocated node
  explicit constexpr isolated_node_tx(T &node, Container &container) noexcept RELOCO_RETURN_TYPESTATE(unconsumed)
      : m_node(&node), m_container(&container) {}

  // Move-only semantics. Banned copy to prevent topological cloning.
  isolated_node_tx(const isolated_node_tx &) = delete;
  isolated_node_tx &operator=(const isolated_node_tx &) = delete;

  constexpr isolated_node_tx(isolated_node_tx &&other) noexcept : m_node(other.m_node), m_container(other.m_container) {
    other.m_node = nullptr;
    other.m_container = nullptr;
  }

  constexpr isolated_node_tx &operator=(isolated_node_tx &&other) noexcept {
    if (this != &other) {
      m_node = other.m_node;
      m_container = other.m_container;
      other.m_node = nullptr;
      other.m_container = nullptr;
    }
    return *this;
  }

  RELOCO_CONSTEXPR20 ~isolated_node_tx() noexcept {
    RELOCO_ASSERT(m_node == nullptr, "isolated_node_tx dropped without being consumed!");
  }

  template <typename Inserter>
  void relink_to(Container &container, Inserter inserter) noexcept RELOCO_CALLABLE_WHEN(unconsumed)
      RELOCO_SET_TYPESTATE(consumed) {
    RELOCO_ASSERT(m_node != nullptr, "relink_to: Called on consumed transaction");
    RELOCO_ASSERT(&container != m_container, "relink_to: Trying to mutate original container");
    inserter(container, *m_node);
    m_node = nullptr; // Node is safely handed off. Consume the typestate.
  }

  // Explicit consumptive sink: Returns to an RTOS memory pool/slab allocator
  template <typename Disposer>
  void release_to(Disposer disposer) noexcept RELOCO_CALLABLE_WHEN(unconsumed) RELOCO_SET_TYPESTATE(consumed) {
    RELOCO_ASSERT(m_node != nullptr, "release_to: Called on consumed transaction");
    disposer(m_node);
    m_node = nullptr; // Node is safely handed off. Consume the typestate.
  }

  // Safe read-only access to the payload
  const T &get() const noexcept RELOCO_CALLABLE_WHEN(unconsumed) {
    RELOCO_ASSERT(m_node != nullptr, "Called on consumed transaction");
    return *m_node;
  }

  T &get_mut() noexcept RELOCO_CALLABLE_WHEN(unconsumed) {
    RELOCO_ASSERT(m_node != nullptr, "Called on consumed transaction");
    return *m_node;
  }

  isolated_node_tx &as_known() & noexcept RELOCO_RETURN_TYPESTATE(unconsumed) {
    RELOCO_ASSERT(m_node != nullptr, "Called on consumed transaction");
    return *this;
  }

  isolated_node_tx &&as_known() && noexcept RELOCO_RETURN_TYPESTATE(unconsumed) {
    RELOCO_ASSERT(m_node != nullptr, "Called on consumed transaction");
    return std::move(*this);
  }

  const isolated_node_tx &as_known() const & noexcept RELOCO_RETURN_TYPESTATE(unconsumed) {
    RELOCO_ASSERT(m_node != nullptr, "Called on consumed transaction");
    return *this;
  }

private:
  T *m_node{nullptr};
  Container *m_container{nullptr};
};

/**
 * @brief Iterator that extracts nodes satisfying a predicate.
 * @tparam Container Intrusive container type.
 * @tparam Pred Selection predicate.
 */
template <typename Container, typename Pred>
class extract_if_iterator : public iterator_adaptor<extract_if_iterator<Container, Pred>,
                                                    isolated_node_tx<Container, typename Container::value_type>> {
  Container *m_container;
  typename Container::iterator m_curr;
  Pred m_pred;

public:
  extract_if_iterator(Container &c, Pred p) noexcept
      : m_container(&c), m_curr(c.begin()), m_pred(static_cast<Pred &&>(p)) {}

  [[nodiscard]] optional<isolated_node_tx<Container, typename Container::value_type>> next_impl() noexcept {
    while (m_curr != m_container->end()) {
      auto &item = *m_curr;

      if (m_pred(item)) {
        // The pipeline performs the unsafe topological mutation internally.
        // `erase` returns the iterator to the element following the one just
        // unlinked (matching `boost::intrusive`/`std::list::erase`), so the
        // traversal cursor advances directly off of it instead of needing a
        // separate lookahead `++m_curr` before mutating.
        m_curr = m_container->erase(m_curr);

        // Yield the safe RAII typestate wrapper downstream
        return isolated_node_tx<Container, typename Container::value_type>{item, *m_container};
      }

      ++m_curr;
    }
    return nullopt;
  }
};

} // namespace reloco