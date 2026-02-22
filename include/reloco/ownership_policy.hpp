#include <reloco/assert.hpp>
#include <reloco/concepts.hpp>
#include <reloco/shared_ptr.hpp>

namespace reloco {
namespace policy {

template <typename Container> struct move_owner {
  using storage_type = Container;
  static Container &get(storage_type &s) { return s; }
  static const Container &get(const storage_type &s) { return s; }
};

template <typename Container> struct unique_owner {
  using storage_type = std::unique_ptr<Container>;

  static Container &get(storage_type &s) {
    RELOCO_ASSERT(s);
    return *s;
  }

  static const Container &get(const storage_type &s) {
    RELOCO_ASSERT(s);
    return *s;
  }
};

template <typename Container> struct shared_owner {
  using storage_type = reloco::shared_ptr<Container>;
  static Container &get(storage_type &s) { return *s; }
  static const Container &get(const storage_type &s) { return *s; }
};

template <typename Container> struct non_owner {
  using storage_type = Container *;
  static Container &get(storage_type &s) {
    RELOCO_ASSERT(s);
    return *s;
  }
  static const Container &get(const storage_type &s) {
    RELOCO_ASSERT(s);
    return *s;
  }
};

} // namespace policy

} // namespace reloco
