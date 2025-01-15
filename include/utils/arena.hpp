#pragma once

#include <cstddef>
#include <iostream>

namespace Norwood {
class Arena {
 public:
  Arena() = default;
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  ~Arena() = default;
  auto Alloc(size_t byte_size) -> char*;
  auto AllocateAligned(size_t byte_size) -> char*;
  
};
}  // namespace Norwood
