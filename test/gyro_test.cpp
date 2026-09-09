// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <chrono>
#include <ctime>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <gtest/gtest.h>

#include <gyro/gyro.h>

namespace {
    using namespace std::chrono_literals;

    TEST(LoopTest, AnIdleLoopReturnsWithoutWaiting) {
        auto *loop = gyro_new(nullptr);
        ASSERT_NE(loop, nullptr);

        EXPECT_EQ(gyro_run(loop), GYRO_COMPLETED);
        EXPECT_EQ(gyro_free(loop), GYRO_COMPLETED);
    }

    TEST(LoopTest, AStopIssuedBeforeTheLoopRunsIsSwallowed) {
        auto *loop = gyro_new(nullptr);
        ASSERT_NE(loop, nullptr);

        gyro_stop(loop);

        // gyro_run() clears the flag on its way in, so this run was never
        // stopped: it comes back because there was nothing to do in the first
        // place. Anything that wants to stop a loop has to wait until it runs.
        EXPECT_EQ(gyro_run(loop), GYRO_COMPLETED);

        EXPECT_EQ(gyro_free(loop), GYRO_COMPLETED);
    }

    /**
     * @brief A loop with one operation outstanding that nothing will ever
     *        satisfy, and no deadline on it.
     *
     * That combination is what makes the backend block indefinitely: the loop
     * has work, so it will not return of its own accord, and it has no timer to
     * wake it. The only thing that can bring it back is gyro_stop(), which is
     * exactly what these tests are about.
     */
    class WakeupTest : public testing::Test {
    protected:
        gyro_t *loop = nullptr;
        gyro_tcp_t *server = nullptr;
        gyro_tcp_t *peer = nullptr; ///< Where a connection would land. None ever does.

        int closed = 0;

        static WakeupTest *current;

        static gyro_cb_status_t OnAccept(gyro_handle_t *, const int status, size_t, void *) {
            // Only ever reached through the teardown that cancels it.
            EXPECT_EQ(status, GYRO_ECANCELED);

            return GYRO_CB_SUCCESS;
        }

        static void OnClose(gyro_handle_t *) {
            WakeupTest::current->closed++;
        }

        /// Runs the loop, and asks another thread to stop it after @p delay.
        std::chrono::milliseconds RunUntilStoppedFrom(const std::chrono::milliseconds delay) {
            std::thread waker([this, delay] {
                std::this_thread::sleep_for(delay);

                gyro_stop(this->loop);
            });

            const auto started = std::chrono::steady_clock::now();
            const int rc = gyro_run(this->loop);
            const auto elapsed = std::chrono::steady_clock::now() - started;

            waker.join();

            EXPECT_EQ(rc, GYRO_STOPPED);

            return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        }

        void SetUp() override {
            WakeupTest::current = this;

            this->loop = gyro_new(nullptr);
            ASSERT_NE(this->loop, nullptr);

            this->server = gyro_tcp_new(this->loop);
            this->peer = gyro_tcp_new(this->loop);
            ASSERT_NE(this->server, nullptr);
            ASSERT_NE(this->peer, nullptr);

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

            ASSERT_EQ(gyro_tcp_bind(this->server, (sockaddr *) &addr, sizeof(addr), GYRO_TCP_REUSEADDR),
                      GYRO_COMPLETED);
            ASSERT_EQ(gyro_tcp_listen(this->server, 16), GYRO_COMPLETED);

            // Nobody is going to connect, and the operation carries no deadline.
            ASSERT_EQ(gyro_tcp_accept(this->server, this->peer, 0, OnAccept, nullptr, nullptr), GYRO_PENDING);
        }

        void TearDown() override {
            WakeupTest::current = this;

            gyro_handle_close(GYRO_HANDLE(this->server), OnClose);
            gyro_handle_close(GYRO_HANDLE(this->peer), OnClose);

            // Closing cancels the accept, so this run has an end of its own and
            // needs nobody to stop it.
            EXPECT_EQ(gyro_run(this->loop), GYRO_COMPLETED);
            EXPECT_EQ(this->closed, 2);

            EXPECT_EQ(gyro_free(this->loop), GYRO_COMPLETED);
        }
    };

    WakeupTest *WakeupTest::current = nullptr;

    TEST_F(WakeupTest, StopFromAnotherThreadBringsABlockedLoopBack) {
        constexpr auto kDelay = 150ms;

        const auto elapsed = RunUntilStoppedFrom(kDelay);

        // Returning at all is the assertion: without the wakeup reaching the
        // backend, this loop had nothing to look at and would have stayed in
        // its wait for ever. The elapsed time says it really did wait, rather
        // than never having blocked in the first place.
        EXPECT_GE(elapsed, kDelay - 20ms) << "the loop came back before it was asked to";
    }

    TEST_F(WakeupTest, AWaitingLoopSleepsRatherThanSpinning) {
        // Signal the wakeup once. From here on it has been triggered, and the
        // backend must have consumed it.
        RunUntilStoppedFrom(50ms);

        // Wall clock cannot tell a sleeping loop from a spinning one, because
        // both come back when they are told to. Processor time can: a wakeup
        // that was signalled and never drained stays ready, and every wait
        // returns at once instead of blocking.
        const auto cpu_before = std::clock();

        const auto elapsed = RunUntilStoppedFrom(200ms);

        const auto cpu = 1000.0 * (double) (std::clock() - cpu_before) / CLOCKS_PER_SEC;

        EXPECT_GE(elapsed, 180ms);
        EXPECT_LT(cpu, 50.0) << "the loop burned " << cpu << " ms of CPU over "
                << elapsed.count() << " ms of waiting";
    }
} // namespace
