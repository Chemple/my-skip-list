#pragma once
#include <concepts>
#include <cstdint>
#include <memory>

#include "lock_free_skip_list.hpp"
#include "simple_skip_list.hpp"
#include "spin_lock.hpp"

template <typename T>
concept LockConcept = requires(T t) {
  { t.lock() } -> std::same_as<void>;
  { t.unlock() } -> std::same_as<void>;
};

template <typename T, typename U, typename SkipListType>
concept SkiplistConcept = requires(SkipListType list, const T& const_key,
                                   const U& const_value, U& mut_value) {
  { list.Get(const_key, mut_value) } -> std::same_as<bool>;
  { list.Put(const_key, const_value) } -> std::same_as<bool>;
};

template <typename T = uint32_t, typename U = uint32_t,
          typename L = NaiveSpinLock, typename S = SkipList<T, U>>
  requires LockConcept<L> && SkiplistConcept<T, U, S>
class MemTable {
 public:
  using key_type = T;
  using value_type = U;
  using lock_type = L;
  using skiplist_type = S;

  const uint32_t tomb = 0xFFFFFFFF;

  explicit MemTable() { skip_list_ = std::make_shared<skiplist_type>(); }

  auto Get(const key_type& key, value_type& value) -> bool {
    auto skiplist_res = skip_list_->Get(key, value);
    if (skiplist_res == true) {
      return value != tomb;
    }
    return false;
  }

  auto Put(const key_type& key, const value_type& value) -> bool {
    state_lock_.lock();
    skip_list_->Put(key, value);
    state_lock_.unlock();
    return true;
  }

  auto Delete(const key_type& key) -> bool {
    state_lock_.lock();
    skip_list_->Put(key, tomb);
    state_lock_.unlock();
    return true;
  }

 private:
  std::shared_ptr<skiplist_type> skip_list_;
  lock_type state_lock_{};
};
