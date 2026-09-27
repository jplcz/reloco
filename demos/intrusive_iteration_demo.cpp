#include <boost/intrusive/list.hpp>
#include <iostream>
#include <reloco/intrusive_iteration.hpp>
#include <reloco/scope_guard.hpp>

using namespace boost::intrusive;

// ========================================================================
// The Intrusive Node Structure
// ========================================================================
struct Task : public list_base_hook<link_mode<auto_unlink>> {
  int id;
  int ticks_remaining;
  bool is_blocked;

  Task(int i, int ticks, bool blocked = false) : id(i), ticks_remaining(ticks), is_blocked(blocked) {}

  // Executes some work. Returns true if the task finishes.
  bool tick() {
    if (ticks_remaining > 0)
      --ticks_remaining;
    return ticks_remaining == 0;
  }
};

// Define an O(1) size-untracked list, perfect for RTOS queues
using TaskList = list<Task, constant_time_size<false>>;

// A quick factory function to create the iterator cleanly
namespace reloco {
template <typename Container, typename Pred> auto extract_if(Container &c, Pred p) {
  return extract_if_iterator<Container, Pred>(c, std::move(p));
}
} // namespace reloco

// ========================================================================
// The Demo Execution
// ========================================================================
int main() {
  // Our RTOS Queues
  TaskList active_queue;
  TaskList blocked_queue;

  // Statically allocated tasks (simulating an RTOS memory pool)
  Task t1(101, 1);       // Will finish on tick 1
  Task t2(102, 5);       // Will survive tick 1
  Task t3(103, 2, true); // Starts blocked

  active_queue.push_back(t1);
  active_queue.push_back(t2);
  active_queue.push_back(t3);

  std::cout << "--- Scheduler Tick Start ---\n";

  // Reusable C++ lambdas for routing topologies
  auto list_inserter = [](TaskList &dest, Task &t) { dest.push_back(t); };

  auto memory_pool_disposer = [](Task *t) {
    // In a real system, this returns the memory to the slab allocator.
    std::cout << "[SYSTEM] Task " << t->id << " memory reclaimed.\n";
  };

  RELOCO_DEFER([&]() { std::cout << "This would put blocked tasks back\n"; });

  // ========================================================================
  // 3. The Functional Pipeline
  // ========================================================================
  reloco::extract_if(active_queue, [](Task &t) {
    // Evaluate state
    if (t.is_blocked)
      return true;             // Extract immediately
    return t.tick();           // Or extract if it finishes this tick
  }).for_each([&](auto &&tx) { // 'tx' is isolated_node_tx<TaskList, Task>
    // 1. Safe Mutation Phase (Node is isolated from all graphs)
    Task &t = tx.get_mut();

    // 2. Routing Phase
    if (t.is_blocked) {
      std::cout << "[ROUTER] Task " << t.id << " is blocked. Moving to Blocked Queue.\n";
      tx.relink_to(blocked_queue, list_inserter);
    } else {
      std::cout << "[ROUTER] Task " << t.id << " finished execution.\n";
      tx.release_to(memory_pool_disposer);
    }

    // If you tried to call tx.relink_to(active_queue, ...) here,
    // the assert(&container != m_container) would instantly abort the program!
  });

  std::cout << "\n--- Scheduler Tick Complete ---\n";

  // Verify Topological State
  std::cout << "Active Queue size:  " << std::distance(active_queue.begin(), active_queue.end()) << "\n";
  std::cout << "Blocked Queue size: " << std::distance(blocked_queue.begin(), blocked_queue.end()) << "\n";

  // The remaining task (t2) is still safely in the active queue!
  if (!active_queue.empty()) {
    std::cout << "Task remaining in active queue: " << active_queue.front().id << "\n";
  }

  return 0;
}