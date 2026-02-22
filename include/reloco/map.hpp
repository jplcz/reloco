#pragma once
#include <boost/intrusive/set.hpp>
#include <reloco/vector.hpp>

namespace reloco {

template <typename K, typename V, typename Compare = std::less<K>,
          typename Alloc = core_allocator>
class map {
  struct MapNode : public boost::intrusive::set_base_hook<> {
    K key;
    V value;

    MapNode(K &&k, V &&v) : key(std::move(k)), value(std::move(v)) {}

    template <typename... KArgs, typename... VArgs>
    static result<MapNode> try_create(std::tuple<KArgs...> k_args,
                                      std::tuple<VArgs...> v_args) noexcept {
      result<K> k_res = create_component<K>(
          std::move(k_args), std::make_index_sequence<sizeof...(KArgs)>{});
      if (!k_res)
        return unexpected(k_res.error());

      result<V> v_res = create_component<V>(
          std::move(v_args), std::make_index_sequence<sizeof...(VArgs)>{});
      if (!v_res)
        return unexpected(v_res.error());

      return MapNode(std::move(*k_res), std::move(*v_res));
    }

    [[nodiscard]] result<MapNode> try_clone() const noexcept {
      auto k_res = clone_component(key);
      if (!k_res)
        return unexpected(k_res.error());

      auto v_res = clone_component(value);
      if (!v_res)
        return unexpected(v_res.error());

      return MapNode(std::move(*k_res), std::move(*v_res));
    }

  private:
    // Helper to detect and call try_create vs Constructor
    template <typename T, typename Tuple, size_t... I>
    static result<T> create_component(Tuple &&args,
                                      std::index_sequence<I...>) noexcept {
      if constexpr (has_try_create<T, decltype(std::get<I>(args))...>) {
        return T::try_create(std::get<I>(std::forward<Tuple>(args))...);
      } else {
        return T(std::get<I>(std::forward<Tuple>(args))...);
      }
    }

    // Helper to detect and call try_clone vs Copy Constructor
    template <typename T>
    static result<T> clone_component(const T &obj) noexcept {
      if constexpr (has_try_clone<T>) {
        return obj.try_clone();
      } else {
        return T(obj);
      }
    }
  };

  struct KeyOfNode {
    using type = const K &;

    const K &operator()(const MapNode &n) const noexcept { return n.key; }
  };

  // Helper to destroy and deallocate a node
  struct NodeDisposer {
    Alloc *alloc;
    void operator()(MapNode *node) {
      node->~MapNode();
      alloc->deallocate(node, sizeof(MapNode));
    }
  };

  Alloc *alloc_;

  using set_t =
      boost::intrusive::set<MapNode, boost::intrusive::key_of_value<KeyOfNode>,
                            boost::intrusive::compare<Compare>>;

  set_t set_;

public:
  using key_type = K;
  using mapped_type = V;
  using value_type = MapNode; // In intrusive, the node IS the value
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = value_type &;
  using const_reference = const value_type &;

  // Iterator types from boost::intrusive
  using iterator = typename set_t::iterator;
  using const_iterator = typename set_t::const_iterator;
  using reverse_iterator = typename set_t::reverse_iterator;
  using const_reverse_iterator = typename set_t::const_reverse_iterator;

  map() noexcept : alloc_(&get_default_allocator()) {}

  explicit map(Alloc &a) noexcept : alloc_(&a) {}

  // Move-only
  map(const map &) = delete;
  map &operator=(const map &) = delete;

  map(map &&other) noexcept
      : alloc_(other.alloc_), set_(std::move(other.set_)) {}

  ~map() { clear(); }

  void clear() noexcept { set_.clear_and_dispose(NodeDisposer{alloc_}); }

  iterator begin() noexcept { return set_.begin(); }
  const_iterator begin() const noexcept { return set_.begin(); }
  const_iterator cbegin() const noexcept { return set_.cbegin(); }

  iterator end() noexcept { return set_.end(); }
  const_iterator end() const noexcept { return set_.end(); }
  const_iterator cend() const noexcept { return set_.cend(); }

  reverse_iterator rbegin() noexcept { return set_.rbegin(); }
  reverse_iterator rend() noexcept { return set_.rend(); }

  [[nodiscard]] bool empty() const noexcept { return set_.empty(); }
  size_type size() const noexcept { return set_.size(); }
  size_type max_size() const noexcept { return size_type(-1); }

  [[nodiscard]] result<map> try_clone() const noexcept {
    map new_map(*alloc_);

    for (const auto &node : set_) {
      auto block = alloc_->allocate(sizeof(MapNode), alignof(MapNode));
      if (!block)
        return unexpected(block.error());
      MapNode *ptr = static_cast<MapNode *>(block->ptr);

      auto node_res = node.try_clone();
      if (!node_res) {
        alloc_->deallocate(ptr, sizeof(MapNode));
        return unexpected(node_res.error());
      }

      new (ptr) MapNode(std::move(*node_res));
      new_map.set_.insert_equal(*ptr);
    }

    return new_map;
  }

  [[nodiscard]] result<V *> try_insert(K key, V value) noexcept {
    // Search for existing
    auto it = set_.find(key);
    if (it != set_.end()) {
      return unexpected(error::already_exists);
    }

    auto block = alloc_->allocate(sizeof(MapNode), alignof(MapNode));
    if (!block)
      return unexpected(block.error());

    MapNode *node = new (block->ptr) MapNode(std::move(key), std::move(value));

    // 4. Link into tree
    set_.insert_equal(*node);
    return &node->value;
  }

  [[nodiscard]] result<void> try_erase(const K &key) noexcept {
    auto it = set_.find(key);
    if (it == set_.end()) {
      return unexpected(error::out_of_range);
    }

    // Unlink from tree and deallocate
    set_.erase_and_dispose(it, NodeDisposer{alloc_});
    return {};
  }

  iterator find(const K &key) noexcept { return set_.find(key); }

  const_iterator find(const K &key) const noexcept { return set_.find(key); }

  bool contains(const K &key) const noexcept { return find(key) != end(); }

  iterator lower_bound(const K &key) noexcept { return set_.lower_bound(key); }

  iterator upper_bound(const K &key) noexcept { return set_.upper_bound(key); }

  std::pair<iterator, iterator> equal_range(const K &key) noexcept {
    return set_.equal_range(key);
  }

  [[nodiscard]] result<V *> try_at(const K &key) noexcept {
    auto it = find(key);
    if (it == end()) {
      return unexpected(error::out_of_range);
    }
    return &(it->value);
  }

  iterator erase(const_iterator it) noexcept {
    return set_.erase_and_dispose(it, NodeDisposer{alloc_});
  }

  iterator erase(const_iterator first, const_iterator last) noexcept {
    return set_.erase_and_dispose(first, last, NodeDisposer{alloc_});
  }

  size_type erase(const K &key) noexcept {
    auto it = find(key);
    if (it != end()) {
      erase(it);
      return 1;
    }
    return 0;
  }

  template <typename... Args>
  [[nodiscard]] result<V *> try_emplace(const K &key, Args &&...args) noexcept {
    auto it = set_.find(key);
    if (it != set_.end()) {
      return &it->value; // STL behavior: return existing if found
    }

    auto block = alloc_->allocate(sizeof(MapNode), alignof(MapNode));
    if (!block)
      return unexpected(error::allocation_failed);
    MapNode *ptr = static_cast<MapNode *>(block->ptr);

    auto res = MapNode::try_create(
        std::make_tuple(key), std::make_tuple(std::forward<Args>(args)...));

    if (!res) {
      alloc_->deallocate(ptr, sizeof(MapNode));
      return unexpected(res.error());
    }

    new (ptr) MapNode(std::move(*res));
    set_.insert_equal(*ptr);
    return &ptr->value;
  }

  void swap(map &other) noexcept {
    std::swap(alloc_, other.alloc_);
    set_.swap(other.set_);
  }

  friend void swap(map &lhs, map &rhs) noexcept { lhs.swap(rhs); }

  map &operator=(map &&other) noexcept {
    if (this != &other) {
      clear(); // Clean up current resources
      alloc_ = other.alloc_;
      set_ = std::move(other.set_);
    }
    return *this;
  }

  Alloc &get_allocator() const noexcept { return *alloc_; }

  void merge(map &&other) noexcept {
    auto it = other.set_.begin();
    while (it != other.set_.end()) {
      auto [inserted_it, success] = set_.insert_unique(*it);

      if (success) {
        // Effectively unlinks from 'other' and links into 'this'
        it = other.set_.erase(it);
      } else {
        ++it;
      }
    }
  }
};

template <typename K, typename V, typename Compare, typename Alloc>
struct is_relocatable<map<K, V, Compare, Alloc>> : std::false_type {};

template <typename K, typename V, typename Compare, typename Alloc>
struct is_fallible_type<map<K, V, Compare, Alloc>> : std::true_type {};

} // namespace reloco