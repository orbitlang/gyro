// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <vector>

#include <gtest/gtest.h>

#include <support/reqstore.h>

namespace {
    using gyro::RequestIndex;
    using gyro::support::RequestStore;

    /// Pages hold 256 requests; anything above it exercises growth.
    constexpr uint32_t kPageSize = 256;

    /**
     * @brief Allocator that counts, and can be told to start failing.
     */
    struct TestAllocator {
        gyro_allocator_t hooks{};

        size_t allocations = 0;
        size_t frees = 0;
        size_t live_bytes = 0;
        int budget = -1; ///< Allocations left before failing; -1 never fails.

        std::vector<std::pair<void *, size_t> > live;

        TestAllocator() {
            this->hooks.alloc = Alloc;
            this->hooks.free = Free;
            this->hooks.ctx = this;
        }

        static void *Alloc(size_t size, void *ctx) {
            auto *self = static_cast<TestAllocator *>(ctx);

            if (self->budget == 0)
                return nullptr;

            if (self->budget > 0)
                self->budget--;

            void *ptr = std::malloc(size);
            if (ptr == nullptr)
                return nullptr;

            self->allocations++;
            self->live_bytes += size;
            self->live.emplace_back(ptr, size);

            return ptr;
        }

        static void Free(void *ptr, void *ctx) {
            auto *self = static_cast<TestAllocator *>(ctx);

            if (ptr == nullptr)
                return;

            for (size_t i = 0; i < self->live.size(); i++) {
                if (self->live[i].first == ptr) {
                    self->live_bytes -= self->live[i].second;
                    self->live.erase(self->live.begin() + static_cast<long>(i));

                    self->frees++;
                    std::free(ptr);

                    return;
                }
            }

            ADD_FAILURE() << "freed a pointer this allocator never handed out";
        }
    };

    RequestIndex MakeIndex(uint64_t generation, uint64_t index) {
        RequestIndex token{};

        token.fields.generation = generation;
        token.fields.index = index;

        return token;
    }

    TEST(RequestStoreTest, EmptyStoreAllocatesNothing) {
        TestAllocator allocator; {
            RequestStore store(&allocator.hooks);
        }

        EXPECT_EQ(0u, allocator.allocations);
        EXPECT_EQ(0u, allocator.live_bytes);
    }

    TEST(RequestStoreTest, AcquireOnAFreshStore) {
        TestAllocator allocator;
        RequestStore store(&allocator.hooks);

        RequestIndex token{};
        GyroRequest *req = store.Acquire(token);

        ASSERT_NE(nullptr, req);
        EXPECT_NE(0u, token._opaque) << "the all-zero token must stay unreachable";
    }

    TEST(RequestStoreTest, AcquireReturnsDistinctRequests) {
        TestAllocator allocator;
        RequestStore store(&allocator.hooks);

        RequestIndex first{};
        RequestIndex second{};

        GyroRequest *a = store.Acquire(first);
        GyroRequest *b = store.Acquire(second);

        ASSERT_NE(nullptr, a);
        ASSERT_NE(nullptr, b);

        EXPECT_NE(a, b);
        EXPECT_NE(first._opaque, second._opaque);
    }

    TEST(RequestStoreTest, ResolveReturnsTheAcquiredRequest) {
        TestAllocator allocator;
        RequestStore store(&allocator.hooks);

        RequestIndex token{};
        GyroRequest *req = store.Acquire(token);
        ASSERT_NE(nullptr, req);

        EXPECT_EQ(req, store.Resolve(token));
    }

    TEST(RequestStoreTest, ResolveRejectsAReleasedToken) {
        TestAllocator allocator;
        RequestStore store(&allocator.hooks);

        RequestIndex token{};
        GyroRequest *req = store.Acquire(token);
        ASSERT_NE(nullptr, req);

        store.Release(req);

        EXPECT_EQ(nullptr, store.Resolve(token)) << "a stale token must not resolve";
    }

    TEST(RequestStoreTest, ResolveRejectsTheInvalidToken) {
        TestAllocator allocator;
        RequestStore store(&allocator.hooks);

        RequestIndex token{};
        ASSERT_NE(nullptr, store.Acquire(token));

        RequestIndex invalid{};
        invalid._opaque = 0;

        EXPECT_EQ(nullptr, store.Resolve(invalid));
    }

    TEST(RequestStoreTest, ResolveRejectsAnIndexPastTheStore) {
        TestAllocator allocator;
        RequestStore store(&allocator.hooks);

        RequestIndex token{};
        ASSERT_NE(nullptr, store.Acquire(token));

        // One page exists, so slots 0..kPageSize-1 are the only valid ones.
        EXPECT_EQ(nullptr, store.Resolve(MakeIndex(1, kPageSize)))
                            << "the first index past the store must be rejected";
        EXPECT_EQ(nullptr, store.Resolve(MakeIndex(1, 1u << 23u)));
    }

    TEST(RequestStoreTest, ReleaseMakesTheSlotReusable) {
        TestAllocator allocator;
        RequestStore store(&allocator.hooks);

        RequestIndex first{};
        GyroRequest *a = store.Acquire(first);
        ASSERT_NE(nullptr, a);

        store.Release(a);

        RequestIndex second{};
        GyroRequest *b = store.Acquire(second);
        ASSERT_NE(nullptr, b);

        EXPECT_EQ(a, b) << "the freed slot should be handed out again";
        EXPECT_NE(first._opaque, second._opaque) << "with a different generation";

        EXPECT_EQ(nullptr, store.Resolve(first));
        EXPECT_EQ(b, store.Resolve(second));
    }

    TEST(RequestStoreTest, GrowsBeyondOnePage) {
        TestAllocator allocator;
        RequestStore store(&allocator.hooks);

        const uint32_t count = kPageSize * 3 + 7;

        std::vector<RequestIndex> tokens;
        std::vector<GyroRequest *> requests;

        for (uint32_t i = 0; i < count; i++) {
            RequestIndex token{};
            GyroRequest *req = store.Acquire(token);

            ASSERT_NE(nullptr, req) << "at acquisition " << i;

            tokens.push_back(token);
            requests.push_back(req);
        }

        for (uint32_t i = 0; i < count; i++)
            EXPECT_EQ(requests[i], store.Resolve(tokens[i])) << "token " << i;

        std::sort(requests.begin(), requests.end());
        EXPECT_EQ(requests.end(), std::unique(requests.begin(), requests.end()))
                            << "two acquisitions returned the same request";
    }

    TEST(RequestStoreTest, SurvivesChurn) {
        TestAllocator allocator;
        RequestStore store(&allocator.hooks);

        std::vector<std::pair<RequestIndex, GyroRequest *> > held;

        for (uint32_t round = 0; round < 4; round++) {
            for (uint32_t i = 0; i < kPageSize + 13; i++) {
                RequestIndex token{};
                GyroRequest *req = store.Acquire(token);

                ASSERT_NE(nullptr, req);
                held.emplace_back(token, req);
            }

            // Release every other one, then check the survivors still resolve.
            for (size_t i = 0; i < held.size(); i += 2)
                store.Release(held[i].second);

            for (size_t i = 0; i < held.size(); i++) {
                if (i % 2 == 0)
                    EXPECT_EQ(nullptr, store.Resolve(held[i].first)) << "round " << round;
                else
                    EXPECT_EQ(held[i].second, store.Resolve(held[i].first)) << "round " << round;
            }

            std::vector<std::pair<RequestIndex, GyroRequest *> > survivors;
            for (size_t i = 1; i < held.size(); i += 2)
                survivors.push_back(held[i]);

            held.swap(survivors);
        }
    }

    TEST(RequestStoreTest, ReportsAllocationFailure) {
        TestAllocator allocator;
        allocator.budget = 0; // fail immediately

        RequestStore store(&allocator.hooks);

        RequestIndex token{};

        EXPECT_EQ(nullptr, store.Acquire(token));
    }

    TEST(RequestStoreTest, SurvivesFailureOfThePageAllocation) {
        TestAllocator allocator;
        allocator.budget = 1; // the directory succeeds, the page does not

        RequestStore store(&allocator.hooks);

        RequestIndex token{};

        EXPECT_EQ(nullptr, store.Acquire(token));
    }

    TEST(RequestStoreTest, FreesEverythingItAllocated) {
        TestAllocator allocator; {
            RequestStore store(&allocator.hooks);

            for (uint32_t i = 0; i < kPageSize * 5; i++) {
                RequestIndex token{};
                ASSERT_NE(nullptr, store.Acquire(token));
            }
        }

        EXPECT_EQ(0u, allocator.live_bytes) << "the store leaked memory";
        EXPECT_EQ(allocator.allocations, allocator.frees);
    }
} // namespace
