#include <concepts>
#include <cstdint>
#include <memory>

#include "simple_skip_list.hpp"
#include "spin_lock.hpp"

template <typename T>
concept Lock = requires(T t) {
  { t.lock() } -> std::same_as<void>;
  { t.unlock() } -> std::same_as<void>;
};

template <typename T = uint32_t, typename U = uint32_t, Lock L = NaiveSpinLock>
class MemTable {
 public:
  using key_type = T;
  using value_type = U;
  using lock_type = L;

  const uint32_t tomb = 0xFFFFFFFF;

  explicit MemTable() { skip_list_ = std::make_shared<SkipList<T, U>>(); }

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

  auto Debug() { skip_list_->Debug(); }

 private:
  std::shared_ptr<SkipList<T, U>> skip_list_;
  lock_type state_lock_{};
};
