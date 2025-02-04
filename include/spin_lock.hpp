#include <atomic>
#include <thread>

#include "atomic"

class NaiveSpinLock {
 public:
  void lock() {
    auto expected = false;
    while (!lock_.compare_exchange_weak(expected, true,
                                        std::memory_order::acquire)) {
      expected = false;
      std::this_thread::yield();
    }
  }
  void unlock() { lock_.store(false, std::memory_order::release); }

 private:
  std::atomic<bool> lock_;
};

class NoLock {
 public:
  void lock() {}
  void unlock() {}
};