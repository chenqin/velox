/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include "velox/common/memory/HashStringAllocator.h"
#include "velox/functions/lib/KllSketch.h"

namespace facebook::velox::functions::kll::test {
namespace {

// Error bound for k = 200.
constexpr double kEpsilon = 0.0133;

// Generate linearly spaced values between [0, 1].
std::vector<double> linspace(int len) {
  VELOX_DCHECK_GE(len, 2);
  std::vector<double> out(len);
  double step = 1.0 / (len - 1);
  for (int i = 0; i < len; ++i) {
    out[i] = i * step;
  }
  return out;
}

TEST(KllSketchTest, oneItem) {
  KllSketch<double> kll;
  EXPECT_EQ(kll.totalCount(), 0);
  kll.insert(1.0);
  EXPECT_EQ(kll.totalCount(), 1);
  kll.finish();
  EXPECT_EQ(kll.estimateQuantile(0.0), 1.0);
  EXPECT_EQ(kll.estimateQuantile(0.5), 1.0);
  EXPECT_EQ(kll.estimateQuantile(1.0), 1.0);
}

TEST(KllSketchTest, exactMode) {
  constexpr int N = 128;
  KllSketch<int> kll(N);
  for (int i = 0; i < N; ++i) {
    kll.insert(i);
    EXPECT_EQ(kll.totalCount(), i + 1);
  }
  kll.finish();
  EXPECT_EQ(kll.estimateQuantile(0.0), 0);
  EXPECT_EQ(kll.estimateQuantile(0.5), N / 2);
  EXPECT_EQ(kll.estimateQuantile(1.0), N - 1);
  auto q = linspace(N);
  auto v = kll.estimateQuantiles(folly::Range(q.begin(), q.end()));
  for (int i = 0; i < N; ++i) {
    EXPECT_EQ(v[i], i);
  }
}

TEST(KllSketchTest, estimationMode) {
  constexpr int N = 1e5;
  constexpr int M = 1001;
  KllSketch<double> kll(200, {}, 0);
  for (int i = 0; i < N; ++i) {
    kll.insert(i);
    EXPECT_EQ(kll.totalCount(), i + 1);
  }
  kll.finish();
  EXPECT_EQ(kll.estimateQuantile(0.0), 0);
  EXPECT_EQ(kll.estimateQuantile(1.0), N - 1);
  auto q = linspace(M);
  auto v = kll.estimateQuantiles(folly::Range(q.begin(), q.end()));
  ASSERT_TRUE(std::is_sorted(std::begin(v), std::end(v)));
  for (int i = 0; i < M; ++i) {
    EXPECT_NEAR(q[i], v[i] / N, kEpsilon);
  }
}

TEST(KllSketchTest, randomInput) {
  constexpr int N = 1e5;
  constexpr int M = 1001;
  KllSketch<double> kll(kDefaultK, {}, 0);
  std::default_random_engine gen(0);
  std::normal_distribution<> dist;
  double values[N];
  for (int i = 0; i < N; ++i) {
    values[i] = dist(gen);
    kll.insert(values[i]);
  }
  EXPECT_EQ(kll.totalCount(), N);
  kll.finish();
  std::sort(std::begin(values), std::end(values));
  auto q = linspace(M);
  auto v = kll.estimateQuantiles(folly::Range(q.begin(), q.end()));
  ASSERT_TRUE(std::is_sorted(std::begin(v), std::end(v)));
  for (int i = 0; i < M; ++i) {
    auto it = std::lower_bound(std::begin(values), std::end(values), v[i]);
    double actualQ = 1.0 * (it - std::begin(values)) / N;
    EXPECT_NEAR(q[i], actualQ, kEpsilon);
  }
}

TEST(KllSketchTest, merge) {
  constexpr int N = 1e4;
  constexpr int M = 1001;
  KllSketch<double> kll1(kDefaultK, {}, 0);
  KllSketch<double> kll2(kDefaultK, {}, 0);
  for (int i = 0; i < N; ++i) {
    kll1.insert(i);
    kll2.insert(2 * N - i - 1);
  }
  kll1.merge(kll2);
  EXPECT_EQ(kll1.totalCount(), 2 * N);
  kll1.finish();
  auto q = linspace(M);
  auto v = kll1.estimateQuantiles(folly::Range(q.begin(), q.end()));
  ASSERT_TRUE(std::is_sorted(std::begin(v), std::end(v)));
  for (int i = 0; i < M; ++i) {
    EXPECT_NEAR(q[i], v[i] / (2 * N), kEpsilon);
  }
}

TEST(KllSketchTest, mergeRandom) {
  constexpr int N = 1e4;
  constexpr int M = 1001;
  std::default_random_engine gen(0);
  std::uniform_int_distribution<> distN(1, N);
  int n1 = distN(gen), n2 = distN(gen);
  std::vector<double> values;
  values.reserve(n1 + n2);
  KllSketch<double> kll1(kDefaultK, {}, 0);
  KllSketch<double> kll2(kDefaultK, {}, 0);
  std::normal_distribution<> distV;
  for (int i = 0; i < n1; ++i) {
    double v = distV(gen);
    values.push_back(v);
    kll1.insert(v);
  }
  for (int i = 0; i < n2; ++i) {
    double v = distV(gen);
    values.push_back(v);
    kll2.insert(v);
  }
  std::sort(values.begin(), values.end());
  kll1.merge(kll2);
  EXPECT_EQ(kll1.totalCount(), n1 + n2);
  kll1.finish();
  auto q = linspace(M);
  auto v = kll1.estimateQuantiles(folly::Range(q.begin(), q.end()));
  ASSERT_TRUE(std::is_sorted(std::begin(v), std::end(v)));
  for (int i = 0; i < M; ++i) {
    auto it = std::lower_bound(std::begin(values), std::end(values), v[i]);
    double actualQ = 1.0 * (it - std::begin(values)) / values.size();
    EXPECT_NEAR(q[i], actualQ, kEpsilon);
  }
}

TEST(KllSketchTest, mergeMultiple) {
  constexpr int N = 1e4;
  constexpr int M = 1001;
  constexpr int kSketchCount = 10;
  std::vector<KllSketch<double>> sketches;
  for (int i = 0; i < kSketchCount; ++i) {
    KllSketch<double> kll(kDefaultK, {}, 0);
    for (int j = 0; j < N; ++j) {
      kll.insert(j + i * N);
    }
    sketches.push_back(std::move(kll));
  }
  KllSketch<double> kll(kDefaultK, {}, 0);
  kll.merge(folly::Range(sketches.begin(), sketches.end()));
  EXPECT_EQ(kll.totalCount(), N * kSketchCount);
  kll.finish();
  auto q = linspace(M);
  auto v = kll.estimateQuantiles(folly::Range(q.begin(), q.end()));
  ASSERT_TRUE(std::is_sorted(std::begin(v), std::end(v)));
  for (int i = 0; i < M; ++i) {
    EXPECT_NEAR(q[i], v[i] / (N * kSketchCount), kEpsilon);
  }
}

TEST(KllSketchTest, mergeEmpty) {
  KllSketch<double> kll, kll2;
  kll.insert(1.0);
  kll.merge(kll2);
  EXPECT_EQ(kll.totalCount(), 1);
  kll.finish();
  EXPECT_EQ(kll.estimateQuantile(0.5), 1.0);
  kll2.merge(kll);
  EXPECT_EQ(kll2.totalCount(), 1);
  kll2.finish();
  EXPECT_EQ(kll2.estimateQuantile(0.5), 1.0);
}

TEST(KllSketchTest, kFromEpsilon) {
  EXPECT_EQ(kFromEpsilon(kEpsilon), kDefaultK);
}

TEST(KllSketchTest, serialize) {
  constexpr int N = 1e5;
  constexpr int M = 1001;
  KllSketch<double> kll;
  for (int i = 0; i < N; ++i) {
    kll.insert(i);
  }
  kll.finish();
  std::vector<char> data(kll.serializedByteSize());
  kll.serialize(data.data());
  auto kll2 = KllSketch<double>::deserialize(data.data());
  auto q = linspace(M);
  auto v = kll.estimateQuantiles(folly::Range(q.begin(), q.end()));
  auto v2 = kll2.estimateQuantiles(folly::Range(q.begin(), q.end()));
  EXPECT_EQ(v, v2);
}

TEST(KllSketchTest, fromRepeatedValue) {
  constexpr int N = 1000;
  constexpr int kTotal = (1 + N) * N / 2;
  constexpr int M = 1001;
  std::vector<KllSketch<int>> sketches;
  for (int n = 0; n <= N; ++n) {
    auto kll = KllSketch<int>::fromRepeatedValue(n, n);
    EXPECT_EQ(kll.totalCount(), n);
    if (n > 0) {
      const double q[] = {0, 0.25, 0.5, 0.75, 1};
      auto v = kll.estimateQuantiles(folly::Range(std::begin(q), std::end(q)));
      for (int x : v) {
        EXPECT_EQ(x, n);
      }
    }
    sketches.push_back(std::move(kll));
  }
  KllSketch<int> kll(kDefaultK, {}, 0);
  kll.merge(folly::Range(sketches.begin(), sketches.end()));
  EXPECT_EQ(kll.totalCount(), kTotal);
  kll.finish();
  auto q = linspace(M);
  auto v = kll.estimateQuantiles(folly::Range(q.begin(), q.end()));
  for (int i = 0; i < M; ++i) {
    double realQ = 0.5 * v[i] * (v[i] - 1) / kTotal;
    EXPECT_NEAR(q[i], realQ, kEpsilon);
  }
}

TEST(KllSketchTest, mergeDeserialized) {
  constexpr int N = 1e4;
  constexpr int M = 1001;
  KllSketch<double> kll1(kDefaultK, {}, 0);
  KllSketch<double> kll2(kDefaultK, {}, 0);
  for (int i = 0; i < N; ++i) {
    kll1.insert(i);
    kll2.insert(2 * N - i - 1);
  }
  std::vector<char> data(kll2.serializedByteSize());
  kll2.serialize(data.data());
  kll1.mergeDeserialized(data.data());
  EXPECT_EQ(kll1.totalCount(), 2 * N);
  kll1.finish();
  auto q = linspace(M);
  auto v = kll1.estimateQuantiles(folly::Range(q.begin(), q.end()));
  ASSERT_TRUE(std::is_sorted(std::begin(v), std::end(v)));
  for (int i = 0; i < M; ++i) {
    EXPECT_NEAR(q[i], v[i] / (2 * N), kEpsilon);
  }
}

// Suppose the number of elements inserted is N
// 1. When N < K, the memory usage should be O(N).
// 2. Otherwise it's \f$ K \sum_i \(\frac{2}{3}\)^i \f$ and it converges to
//    about O(3K).
TEST(KllSketchTest, memoryUsage) {
  HashStringAllocator alloc(memory::MappedMemory::getInstance());
  KllSketch<int64_t, StlAllocator<int64_t>> kll(
      1024, StlAllocator<int64_t>(&alloc));
  EXPECT_LE(alloc.retainedSize() - alloc.freeSpace(), 64);
  kll.insert(0);
  EXPECT_LE(alloc.retainedSize() - alloc.freeSpace(), 64);
  for (int i = 1; i < 1024; ++i) {
    kll.insert(i);
  }
  EXPECT_LE(alloc.retainedSize() - alloc.freeSpace(), 8500);
  for (int i = 1024; i < 8192; ++i) {
    kll.insert(i);
  }
  EXPECT_LE(alloc.retainedSize() - alloc.freeSpace(), 28000);
}

} // namespace
} // namespace facebook::velox::functions::kll::test
