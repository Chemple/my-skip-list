#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <vector>

#include "random_gen.hpp"

template <typename T = uint32_t, typename U = uint32_t>
struct Node {
  using key_type = T;
  using value_type = U;
  using NodePtr = Node*;
  T k_;
  U v_;
  std::atomic<NodePtr> next_lists_[];

  auto static GetNodeSize(int32_t max_node_level) {
    // NOTE(shiwen): remember the size of struct which contains the flexible
    // array.
    return sizeof(Node) + (max_node_level + 1) * sizeof(std::atomic<NodePtr>);
  }

  auto LoadNext(int32_t level) -> NodePtr {
    return next_lists_[level].load(std::memory_order_acquire);
  }

  auto StoreNext(int32_t level, NodePtr node_ptr) {
    next_lists_[level].store(node_ptr, std::memory_order::release);
  }
};

// NOTE(shiwen): Multiple threads can read the skiplist at the same time, but
// write operations must be mutually exclusive.
template <typename T = uint32_t, typename U = uint32_t>
struct SkipList {
  using key_type = T;
  using value_type = U;
  using NodePtr = Node<key_type, value_type>*;

  enum { Kmax_level = 15 };  // the height is 16.
  enum { Kp = 4 };
  NodePtr head_;
  std::atomic<int32_t> level_;  // the skiplist level (initially 0)

  // key_type first_key_;
  // key_type last_key_;

  Random rnd_;

  explicit SkipList();
  SkipList(SkipList&& other) = delete;
  ~SkipList();

  auto GetRandomLevel() -> int32_t;
  auto Put(const key_type& key, const value_type& value) -> bool;
  auto Get(const key_type& key, value_type& value) const -> bool;
};

template <typename T, typename U>
SkipList<T, U>::SkipList() : rnd_(time(nullptr)) {
  auto head_node_size = Node<>::GetNodeSize(Kmax_level);
  // NOTE(shiwen): use malloc to allocate memories, can be optimaized by arena.
  head_ = static_cast<NodePtr>(malloc(head_node_size));

  // TODO(shiwen): did we need to initial the head_ ?
  // BUG(shiwen): placement new to initialize atomics
  // new (head_) Node<T, U>();

  // NOTE(shiwen): change this, min value of the key_type
  head_->k_ = 0;
  for (auto i = 0; i <= Kmax_level; i++) {
    head_->StoreNext(i, nullptr);
  }
  level_.store(0, std::memory_order_relaxed);
}

template <typename T, typename U>
SkipList<T, U>::~SkipList() {
  NodePtr current = head_;
  while (current) {
    NodePtr next = current->LoadNext(0);
    free(current);
    current = next;
  }
}

template <typename T, typename U>
auto SkipList<T, U>::Get(const key_type& key, value_type& value) const -> bool {
  // NOTE(shiwen): check [first_key, last_key].
  auto cur_node = head_;
  for (auto cur_node_level = level_.load(std::memory_order_acquire);
       cur_node_level >= 0; cur_node_level--) {
    while (true) {
      NodePtr next = cur_node->LoadNext(cur_node_level);
      if (next == nullptr || next->k_ > key) {
        break;
      }
      if (next->k_ == key) {
        // NOTE(shiwen): if dupliacate key, need to get the bottom value.
        if (cur_node_level == 0) {
          value = next->v_;
          return true;
        }
        break;
      }
      cur_node = next;
    }
  }
  return false;
}

template <typename T, typename U>
auto SkipList<T, U>::GetRandomLevel() -> int32_t {
  auto level = 0;
  while (level < Kmax_level && rnd_.OneIn(Kp)) {
    ++level;
  }
  return level;
}

// NOTE(shiwen): Additional synchronization mechanisms should be added at the
// upper layer to ensure that only one thread can call the put method at a time.
template <typename T, typename U>
auto SkipList<T, U>::Put(const key_type& key, const value_type& value) -> bool {
  auto prevs = std::vector<NodePtr>(Kmax_level + 1);
  auto nexts = std::vector<NodePtr>(Kmax_level + 1);

  auto old_level = level_.load(std::memory_order_acquire);
  auto cur_node = head_;
  for (auto cur_node_level = old_level; cur_node_level >= 0; cur_node_level--) {
    while (true) {
      NodePtr next_node = cur_node->LoadNext(cur_node_level);
      if (next_node == nullptr || next_node->k_ >= key) {
        prevs[cur_node_level] = cur_node;
        nexts[cur_node_level] = next_node;
        break;
      }
      cur_node = next_node;
    }
  }

  // init the new node
  auto new_node_level = GetRandomLevel();
  auto new_node_size = Node<>::GetNodeSize(new_node_level);
  auto new_node = static_cast<NodePtr>(malloc(new_node_size));
  new_node->k_ = key;
  new_node->v_ = value;
  if (new_node_level > level_) {
    // BUG(shiwen): Xiaopeng mentioned that two atomic variables should not
    // appear in the same function.
    for (auto level = old_level + 1; level <= new_node_level; level++) {
      prevs[level] = head_;
      nexts[level] = nullptr;
    }
    level_.store(new_node_level, std::memory_order_release);
  }

  // NOTE(shiwen): these operations do not need sync mechanisms.
  for (auto level = 0; level <= new_node_level; level++) {
    assert(level <= Kmax_level);
    new_node->StoreNext(level, nexts[level]);
  }

  for (auto level = 0; level <= new_node_level; level++) {
    assert(level <= Kmax_level);
    prevs[level]->StoreNext(level, new_node);
  }

  return true;
}
