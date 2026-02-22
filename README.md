# Reloco (Reliable Components)

Reloco is a hardened, zero-exception, C++23 systems programming library designed for 
environments where reliability is primary concern. 

It provides "Safety by Default" alternatives to the C++ Standard Library (STL), 
ensuring that every failure—from out-of-bounds access to memory exhaustion—is 
handled explicitly through ``result<T>`` patterns.

## Key Philosophies

* No Exceptions: All fallible operations return a ``result<T, error>`` type. No ``throw``, no ``try/catch``.
* No Undefined Behavior: Operations that are UB in the STL (like span[999]) are 
  either guarded by ``RELOCO_ASSERT`` or handled via fallible ``try_`` APIs.
* Methods that bypass safety checks are explicitly prefixed with ``unsafe_`` to facilitate easy auditing.
* Every container supports custom allocators that can fail gracefully, preventing "silent" OOM crashes.

## Core Components

1. Hardened Smart Pointers

    ``shared_ptr``, ``weak_ptr``, and ``unique_ptr`` with support for Combined Allocation 
    (object and control block in one slab) and Fallible Factories.
    
    ```cpp
    auto res = reloco::try_allocate_combined_shared<Node>(allocator, "Data");
    if (res) {
        auto node = std::move(*res);
        // Use node...
    }
    ```

2. Hardened Span & Array

    Views and containers that replace UB with deterministic failure. Supports Structured Bindings and C++20 Ranges.

    ```cpp
    reloco::array<int, 3> arr = {10, 20, 30};
    auto [x, y, z] = arr; // Structured bindings support
    
    auto res = arr.try_at(5);
    if (!res) {
        // Handle error::out_of_bounds
    }
    ```
3. Allocation System

A suite of allocators that return ``result<mem_block>`` instead of throwing ``std::bad_alloc``.

4. Safe construction

In standard C++, a constructor's only way to signal failure is by 
throwing an exception. In ``reloco``, we use a two-stage approach:

* **Memory Reservation:** The wrapper (``fallible_constructed`` or ``fallible_allocated``) reserves memory for the object.

* **Explicit Initialization:** The try_init() method performs the actual construction and returns a ``result<void>``.

To prevent users from accidentally creating "half-initialized" objects on the stack or heap, 
the target ``class T`` must require a ``reloco::detail::constructor_key<T>``.


* Only the fallible wrappers are friends of the key.


* User cannot call new T() or T object because they cannot provide the required key.

Example:

```cpp
class MyResource {
public:
    using reloco_fallible_t = void; // Satisfies is_fallible_initializable

    // Gated Constructor
    MyResource(reloco::detail::constructor_key<MyResource>) { ... }

    // Gated Initialization
    result<void> try_init(reloco::detail::constructor_key<MyResource>) {
        if (/* hardware fail */) return std::unexpected(reloco::error::unsupported_operation);
        return {};
    }
};
```

There are 3 variants:


- ``fallible_constructed<T>`` with inline storage, best for small objects or stack
  allocated resources. Object is relocatable only if T is relocatable


- ``fallible_allocated<T>`` which used dynamic storage allocated via instance of
  ``fallible_allocator``. This type is always relocatable because moving pointers
  doesn't change address of T. 

- ``static_fallible_constructed<T>`` which is the same as ``fallible_constructed<T>``,
  except destructor is not generated to bypass ``__cxa_atexit`` dependency. This class
  must be used only for e.g. global statics where destructors don't matter

### Important Note on Movable Objects

When working with movable types, keep the following lifecycle rules in mind:

1. ``try_init`` is used only when creating **new** object. This method performs the unique, fallible setup.


2. Moves transfer state, not initialization. When you move a ``fallible_constructed<T>``, the wrapper uses T's move constructor. This transfers the "live" state from the source to the destination.


3. You must not (and cannot) call ``try_init()`` on a moved-to object. Calling ``try_init()`` again would either do nothing or potentially corrupt the transferred state.

## Naming Conventions

Reloco uses a strict naming convention to signal safety and performance characteristics:

| Prefix | Behavior | Return Type | Use Case |
| ------ | -------- | ----------- | -------- |
| try_ | Checked & Fallible | result<T> | Untrusted input |
| (None) | Checked & Hardened | T& / void | Standard logic; ``RELOCO_ASSERT`` |
| unsafe_ | Unchecked | T& / T* | Performance-critical hot loops |


## The Reloco Assert System

``RELOCO_ASSERT`` is always enabled by default, even in Release builds. 
It is designed to trap and halt the program rather than allowing execution with a corrupted state.

* Set a custom global handler to log crash information before the trap.
* Uses ``[[unlikely]]`` and compiler intrinsics to minimize overhead.
* Can be disabled via ``RELOCO_DISABLE_ASSERT`` for absolute peak performance

## Installation & Integration

Reloco is a header-only library. Simply include the ``reloco`` directory in your project.

```cmake
target_include_directories(my_project PRIVATE path/to/reloco/include)
```

## Using the Reloco CMake Package

Reloco exports a modern CMake target named ``reloco::reloco``. 
Using this target automatically handles header include paths and 
required C++23 compiler features.

1. Basic Integration (Find Package)

If Reloco is installed in your system path (e.g., ```/usr/local``` or 
via a package manager), you can pull it into your project with code like below:

```cmake
find_package(reloco REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE reloco::reloco)
```

2. Modern Integration (FetchContent)

```cmake
include(FetchContent)

FetchContent_Declare(
reloco
GIT_REPOSITORY https://github.com/jplcz/reloco.git
GIT_TAG        master
)

FetchContent_MakeAvailable(reloco)

target_link_libraries(my_app PRIVATE reloco::reloco)
```

## Requirements

* Compiler: C++23 compliant

* Standard Library

Technically there would be nothing preventing use of the library with
older C++ standards if components like std::expected were ported.
