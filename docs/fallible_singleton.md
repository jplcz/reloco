In embeded systems, global state is often unavoidable, however standard C++ global 
initialization is prone to the Static Initialization Order Fiasco and cannot handle 
failures (like a mutex failing to initialize).

The ``reloco`` singleton wrappers solve this by combining Lazy Initialization 
with Fallible Error Handling.

## ``fallible_singleton<T>``

This is a **single-threaded** manager designed for controlled boot-time initialization. 
It ensures that a resource is only initialized the first time it is requested.

* Best for stages where concurrency is not yet an issue.

* Uses ``static_fallible_constructed<T>`` internally, meaning the destructor is 
  eliminated to avoid exit routines.

* Not thread-safe. Concurrent calls to ``instance()`` may result in multiple initializations.


## ``atomic_fallible_singleton<T, Traits>``

This is a thread-safe variant using **Double-Checked Locking** (DCLP) 
and atomic state. It's almost drop in replacement for ``fallible_singleton``,
where concurrency is necessary, however ``instance()`` calls will take
some kind of mutex as argument.

## The ``singleton_lock_traits`` Concept

To remain agnostic of the specific locking primitive, the atomic
singleton uses a Trait Policy.

```cpp
// Example: Wrapping a spinlock
struct kernel_spin_policy {
    using lock_type = spinlock_t;
    static void lock(lock_type& l)   { spin_lock(&l); }
    static void unlock(lock_type& l) { spin_unlock(&l); }
};

using NetManager = reloco::atomic_fallible_singleton<network_manager, kernel_spin_policy>;

// A global lock managed by the subsystem
static DEFINE_SPINLOCK(net_init_lock);

reloco::result<network_manager*> get_net_manager() {
    return NetManager::instance(net_init_lock);
}
```
