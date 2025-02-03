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

TEST(MemTableTest, ScalePutGet) {
  auto mt = MemTable<>{};
  constexpr int scale = 1000;
  constexpr int initial_insert = 500;
  constexpr int num_search_threads = 4;  // 查询线程数
  constexpr int num_insert_threads = 2;  // 插入线程数
  std::atomic<bool> stop_flag{false};

  // 先插入前 500 个元素
  for (int i = 1; i <= initial_insert; i++) {
    EXPECT_TRUE(mt.Put(i, i));
  }

  // 查询线程：随机查询前 500 个元素
  auto search_worker = [&mt, &stop_flag]() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, initial_insert);

    while (!stop_flag.load()) {
      int key = dis(gen);  // 随机生成 key
      uint32_t value;
      if (mt.Get(key, value)) {
        EXPECT_EQ(value, key);  // 验证查询结果
      }
    }
  };

  // 插入线程：插入后 500 个元素
  auto insert_worker = [&mt](int thread_id) {
    int start = initial_insert + 1 +
                thread_id * (scale - initial_insert) / num_insert_threads;
    int end = initial_insert +
              (thread_id + 1) * (scale - initial_insert) / num_insert_threads;
    for (int i = start; i <= end; i++) {
      EXPECT_TRUE(mt.Put(i, i));  // 插入元素
    }
  };

  // 启动查询线程
  std::vector<std::thread> search_threads;
  for (int i = 0; i < num_search_threads; i++) {
    search_threads.emplace_back(search_worker);
  }

  // 启动插入线程
  std::vector<std::thread> insert_threads;
  for (int i = 0; i < num_insert_threads; i++) {
    insert_threads.emplace_back(insert_worker, i);
  }

  // 让查询线程运行一段时间
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // 停止查询线程
  stop_flag.store(true);

  // 等待查询线程完成
  for (auto& t : search_threads) {
    t.join();
  }

  // 等待插入线程完成
  for (auto& t : insert_threads) {
    t.join();
  }

  // 验证所有 1000 个元素
  uint32_t value;
  for (int i = 1; i <= scale; i++) {
    EXPECT_TRUE(mt.Get(i, value));
    EXPECT_EQ(value, i);
  }

  // 删除所有元素
  for (int i = 1; i <= scale; i++) {
    EXPECT_TRUE(mt.Delete(i));
  }

  // 验证删除后元素不存在
  for (int i = 1; i <= scale; i++) {
    EXPECT_FALSE(mt.Get(i, value));
  }
}
