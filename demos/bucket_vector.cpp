#include <iostream>
#include <reloco/bucket_allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/expected.hpp>
#include <reloco/mutex.hpp>
#include <reloco/string.hpp>
#include <reloco/vector.hpp>

using namespace reloco;

int main() {
  std::cout << "--- Reloco Bucket Allocator & Vector Demo ---\n";

  // Initialize the Bucket Allocator
  // Buckets: 32, 64, 128, 256 bytes.
  // Alignment: 8. Upstream slabs: 64KB.
  bucket_allocator<null_mutex, 32, 64, 128, 256> pool(8, default_allocator(), 65536);

  // Grab the type-erased reference to pass around
  allocator_ref alloc = pool.ref();

  // Fallibly allocate the Vector itself from the pool
  // We use .expect() from your new expected.hpp to safely unwrap or assert!
  auto vec = vector<string>::try_allocate(alloc, 4).expect("Fatal: Failed to allocate vector");

  // Create strings of varying lengths
  const char *messages[] = {
      "Short string",                      // Fits in 32-byte bucket
      "This is a slightly longer string.", // Fits in 64-byte bucket
      "This string is definitively much longer and will require a larger capacity to hold its contents." // Fits in
                                                                                                         // 128-byte
                                                                                                         // bucket
  };

  for (const char *msg : messages) {
    // Fallibly allocate the string's character buffer from the same pool
    auto str = string::try_allocate(alloc, msg).expect("Fatal: Failed to allocate string");

    // Fallibly push to the vector (automatically resizes vector via pool if needed)
    vec.try_push_back(std::move(str)).expect("Fatal: Failed to push string to vector");
  }

  // Iterate and Print
  std::cout << "\nContents of reloco::vector:\n";
  for (std::size_t i = 0; i < vec.size(); ++i) {
    std::cout << "[" << i << "] (" << vec[i].size() << " chars) : " << std::string_view(vec[i]) << "\n";
  }

  // Demonstrate Zero-Copy Expansion
  std::cout << "\n--- Zero-Copy Expansion Test ---\n";

  // The first string is 12 chars + null terminator = 13 bytes.
  // It was allocated in the 32-byte bucket.
  // We can append more characters, and because the new total length
  // STILL fits inside the 32-byte physical block, bucket_allocator's
  // `try_expand_in_place` will succeed instantly with ZERO memory copying!
  vec[0].try_append(" (Expanded!)").expect("Fatal: Failed to append to string");

  std::cout << "Successfully appended: " << std::string_view(vec[0]) << "\n";

  // As `vec` goes out of scope, it destroys the strings, and all memory
  // automatically routes back to the correct physical buckets in the pool.
  return 0;
}