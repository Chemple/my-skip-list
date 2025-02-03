#include <cstdint>
#include <random>

#include "gtest/gtest.h"
#include "simple_memtable.hpp"

TEST(MemTableTest, BasicPutGet) {
  auto mt = MemTable<>{};
  uint32_t value;

  EXPECT_FALSE(mt.Get(1, value));
  EXPECT_TRUE(mt.Put(1, 100));
  EXPECT_TRUE(mt.Put(1, 200));
  mt.Debug();
  EXPECT_TRUE(mt.Get(1, value));
  EXPECT_EQ(200, value);
}

TEST(MemTableTest, OverwriteValue) {
  auto mt = MemTable<>{};

  uint32_t value;

  mt.Put(1, 100);
  mt.Put(1, 200);
  EXPECT_TRUE(mt.Get(1, value));
  EXPECT_EQ(200, value);
}

TEST(MemTableTest, DeleteExisting) {
  auto mt = MemTable<>{};

  uint32_t value;

  mt.Put(1, 100);
  EXPECT_TRUE(mt.Delete(1));
  EXPECT_FALSE(mt.Get(1, value));
}

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <random>
#include <thread>
#include <vector>

TEST(MemTableTest, ScalePutGet) {
  auto mt = MemTable<>{};
  constexpr int scale = 32768;
  constexpr int initial_insert = 8192;
  constexpr int num_search_threads = 8;
  constexpr int num_insert_threads = 8;
  std::atomic<bool> stop_flag{false};

  std::vector<int> keys(scale);
  for (int i = 0; i < scale; i++) {
    keys[i] = i + 1;  // 1 到 scale
  }
  std::random_device rd;
  std::shuffle(keys.begin(), keys.end(), std::mt19937(rd()));

  for (int i = 0; i < initial_insert; i++) {
    EXPECT_TRUE(mt.Put(keys[i], keys[i]));
  }

  auto search_worker = [&mt, &stop_flag, &keys, initial_insert]() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, initial_insert - 1);

    while (!stop_flag.load()) {
      int idx = dis(gen);
      int key = keys[idx];
      uint32_t value;
      if (mt.Get(key, value)) {
        EXPECT_EQ(value, key);
      }
    }
  };

  auto insert_worker = [&mt, &keys, initial_insert, scale](int thread_id) {
    int start = initial_insert +
                thread_id * (scale - initial_insert) / num_insert_threads;
    int end = initial_insert +
              (thread_id + 1) * (scale - initial_insert) / num_insert_threads;
    for (int i = start; i < end; i++) {
      EXPECT_TRUE(mt.Put(keys[i], keys[i]));  // 插入元素
    }
  };

  std::vector<std::thread> search_threads;
  for (int i = 0; i < num_search_threads; i++) {
    search_threads.emplace_back(search_worker);
  }

  std::vector<std::thread> insert_threads;
  for (int i = 0; i < num_insert_threads; i++) {
    insert_threads.emplace_back(insert_worker, i);
  }

  for (auto& t : insert_threads) {
    t.join();
  }

  stop_flag.store(true);

  for (auto& t : search_threads) {
    t.join();
  }

  uint32_t value;
  for (int i = 0; i < scale; i++) {
    EXPECT_TRUE(mt.Get(keys[i], value));
    EXPECT_EQ(value, keys[i]);
  }

  // mt.Debug();

  for (int i = 0; i < scale; i++) {
    EXPECT_TRUE(mt.Delete(keys[i]));
  }

  for (int i = 0; i < scale; i++) {
    EXPECT_FALSE(mt.Get(keys[i], value));
  }
}