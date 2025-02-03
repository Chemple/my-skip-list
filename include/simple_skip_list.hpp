#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <vector>

// NOTE(shiwen): copy from leveldb.
class Random {
 private:
  uint32_t seed_;

 public:
  explicit Random(uint32_t s) : seed_(s & 0x7fffffffu) {
    // Avoid bad seeds.
    if (seed_ == 0 || seed_ == 2147483647L) {
      seed_ = 1;
    }
  }
  uint32_t Next() {
    static const uint32_t M = 2147483647L;  // 2^31-1
    static const uint64_t A = 16807;        // bits 14, 8, 7, 5, 2, 1, 0
    // We are computing
    //       seed_ = (seed_ * A) % M,    where M = 2^31-1
    //
    // seed_ must not be zero or M, or else all subsequent computed values
    // will be zero or M respectively.  For all other values, seed_ will end
    // up cycling through every number in [1,M-1]
    uint64_t product = seed_ * A;

    // Compute (product % M) using the fact that ((x << 31) % M) == x.
    seed_ = static_cast<uint32_t>((product >> 31) + (product & M));
    // The first reduction may overflow by 1 bit, so we may need to
    // repeat.  mod == M is not possible; using > allows the faster
    // sign-bit-based test.
    if (seed_ > M) {
      seed_ -= M;
    }
    return seed_;
  }
  // Returns a uniformly distributed value in the range [0..n-1]
  // REQUIRES: n > 0
  uint32_t Uniform(int n) { return Next() % n; }

  // Randomly returns true ~"1/n" of the time, and false otherwise.
  // REQUIRES: n > 0
  bool OneIn(int n) { return (Next() % n) == 0; }

  // Skewed: pick "base" uniformly from range [0,max_log] and then
  // return "base" random bits.  The effect is to pick a number in the
  // range [0,2^max_log-1] with exponential bias towards smaller numbers.
  uint32_t Skewed(int max_log) { return Uniform(1 << Uniform(max_log + 1)); }
};

template <typename T = uint32_t, typename U = uint32_t>
struct Node {
  using key_type = T;
  using value_type = U;
  using NodePtr = Node*;
  T k_;
  U v_;
  std::atomic<int32_t> node_level_;  // NOTE(shiwen): current node level.
  std::atomic<NodePtr> next_lists_[];

  auto static GetNodeSize(int32_t max_node_level) {
    // NOTE(shiwen): remember the size of struct which contains the flexible
    // array.
    return sizeof(Node) + (max_node_level + 1) * sizeof(std::atomic<NodePtr>);
  }

  auto LoadNext(int32_t level) -> NodePtr {
    assert(level <= node_level_.load(std::memory_order_acquire));
    return next_lists_[level].load(std::memory_order_acquire);
  }

  auto StoreNext(int32_t level, NodePtr node_ptr) {
    assert(level <= node_level_.load(std::memory_order_acquire));
    next_lists_[level].store(node_ptr, std::memory_order::release);
  }

  auto GetLevel() { return node_level_.load(std::memory_order_acquire); }
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

  bool Debug();
};

template <typename T, typename U>
bool SkipList<T, U>::Debug() {
  // Iterate through all levels and print node information
  NodePtr current = head_;
  std::vector<NodePtr> prevs(Kmax_level + 1, nullptr);
  std::cout << "Debugging SkipList:" << std::endl;
  std::cout << "Current level: " << level_.load(std::memory_order_acquire)
            << std::endl;

  for (int level = level_.load(std::memory_order_acquire); level >= 0;
       --level) {
    current = head_;
    std::cout << "Level " << level << ":" << std::endl;
    while (current) {
      // Print node information
      std::cout << "Node: k=" << current->k_ << ", v=" << current->v_
                << ", level=" << current->GetLevel();

      // Print next nodes (forward pointers) and their status
      for (int lvl = 0; lvl <= current->GetLevel(); ++lvl) {
        NodePtr next_node = current->LoadNext(lvl);
        std::cout << ", Level " << lvl << ": ";
        if (next_node) {
          std::cout << "next (k=" << next_node->k_ << ")";
        } else {
          std::cout << "next (nullptr)";
        }
      }
      std::cout << std::endl;

      // Move to the next node at the current level
      current = current->LoadNext(level);
    }
  }

  return true;
}

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
  head_->node_level_.store(Kmax_level, std::memory_order_relaxed);
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
  new_node->node_level_.store(new_node_level, std::memory_order_relaxed);
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
