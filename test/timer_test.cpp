// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <gyro/gyro.h>

namespace {
    using namespace std::chrono_literals;
    using Clock = std::chrono::steady_clock;

    /// What the callbacks saw, and when.
    struct Ticks {
        int calls = 0;
        int last_status = GYRO_PENDING;
        gyro_handle_t *last_handle = (gyro_handle_t *) 1; ///< Anything but the NULL a timer must pass.
        std::vector<Clock::time_point> at;

        /// How many times to answer GYRO_CB_CONTINUE before GYRO_CB_SUCCESS.
        int keep_going = 0;

        /// Cancelled from inside the callback once this many calls are in.
        int cancel_at = 0;
        gyro_t *loop = nullptr;
        gyro_request_t token = gyro_request_invalid();
    };

    gyro_cb_status_t OnTick(gyro_handle_t *handle, const int status, size_t, void *data) {
        auto *t = (Ticks *) data;

        t->calls++;
        t->last_status = status;
        t->last_handle = handle;
        t->at.push_back(Clock::now());

        if (t->cancel_at != 0 && t->calls == t->cancel_at)
            EXPECT_EQ(gyro_request_cancel(t->loop, t->token), GYRO_COMPLETED);

        return t->calls <= t->keep_going ? GYRO_CB_CONTINUE : GYRO_CB_SUCCESS;
    }

    std::chrono::milliseconds Between(const Clock::time_point a, const Clock::time_point b) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(b - a);
    }

    class TimerTest : public testing::Test {
    protected:
        gyro_t *loop = nullptr;

        void SetUp() override {
            this->loop = gyro_new(nullptr);
            ASSERT_NE(this->loop, nullptr);
        }

        void TearDown() override {
            EXPECT_EQ(gyro_free(this->loop), GYRO_COMPLETED);
        }
    };

    TEST_F(TimerTest, AOneShotTimerFiresOnceAndTheLoopRunsDry) {
        Ticks ticks;
        gyro_request_t token;

        const auto armed = Clock::now();

        ASSERT_EQ(gyro_timer_start(this->loop, 30, OnTick, &ticks, &token), GYRO_PENDING);
        ASSERT_TRUE(GYRO_REQUEST_IS_VALID(token));

        // Nothing stops the loop: it has to come back on its own once the timer
        // has fired and nothing else is outstanding.
        EXPECT_EQ(gyro_run(this->loop), GYRO_COMPLETED);

        ASSERT_EQ(ticks.calls, 1);
        EXPECT_EQ(ticks.last_status, GYRO_COMPLETED);
        EXPECT_EQ(ticks.last_handle, nullptr);
        EXPECT_GE(Between(armed, ticks.at[0]), 30ms) << "fired early";
    }

    TEST_F(TimerTest, AZeroTimerFiresOnTheNextTurnAndNeverInTheCall) {
        Ticks ticks;

        ASSERT_EQ(gyro_timer_start(this->loop, 0, OnTick, &ticks, nullptr), GYRO_PENDING);

        // The promise a suspended fiber depends on: the call returns before
        // the callback has run, always.
        EXPECT_EQ(ticks.calls, 0);

        EXPECT_EQ(gyro_run(this->loop), GYRO_COMPLETED);

        EXPECT_EQ(ticks.calls, 1);
        EXPECT_EQ(ticks.last_status, GYRO_COMPLETED);
    }

    TEST_F(TimerTest, ReturningContinueReArmsForTheSameIntervalUntilSuccess) {
        Ticks ticks;
        ticks.keep_going = 2; // three firings in all

        const auto armed = Clock::now();

        ASSERT_EQ(gyro_timer_start(this->loop, 15, OnTick, &ticks, nullptr), GYRO_PENDING);

        // Runs dry only once the callback has said it is done, so returning
        // here with exactly three calls is the assertion that GYRO_CB_SUCCESS
        // ended it and that nothing was left counted against the loop.
        EXPECT_EQ(gyro_run(this->loop), GYRO_COMPLETED);

        ASSERT_EQ(ticks.calls, 3);
        EXPECT_GE(Between(armed, ticks.at[2]), 45ms) << "three intervals have to have passed";
        EXPECT_GE(Between(ticks.at[0], ticks.at[1]), 15ms);
        EXPECT_GE(Between(ticks.at[1], ticks.at[2]), 15ms);
    }

    TEST_F(TimerTest, APeriodicTimerSpacesTheFirstFiringApartFromTheRest) {
        Ticks ticks;
        ticks.keep_going = 2;

        const auto armed = Clock::now();

        // Now, then every 25 ms: the shape gyro_timer_start() cannot express.
        ASSERT_EQ(gyro_timer_start_periodic(this->loop, 0, 25, OnTick, &ticks, nullptr), GYRO_PENDING);

        EXPECT_EQ(gyro_run(this->loop), GYRO_COMPLETED);

        ASSERT_EQ(ticks.calls, 3);
        EXPECT_LT(Between(armed, ticks.at[0]), 10ms) << "the first firing was due at once";
        EXPECT_GE(Between(ticks.at[0], ticks.at[1]), 25ms);
        EXPECT_GE(Between(ticks.at[1], ticks.at[2]), 25ms);
    }

    TEST_F(TimerTest, CancellingAPeriodicTimerFromItsOwnCallbackReportsOnceMore) {
        Ticks ticks;
        ticks.keep_going = 100; // would go on for a long time
        ticks.cancel_at = 2;
        ticks.loop = this->loop;

        ASSERT_EQ(gyro_timer_start_periodic(this->loop, 0, 5, OnTick, &ticks, &ticks.token), GYRO_PENDING);

        EXPECT_EQ(gyro_run(this->loop), GYRO_COMPLETED);

        // Two real firings, then the cancellation posted from the second one
        // is acted on after the re-arm and reports as the third and last call.
        // That is the extra callback the header warns about, and the reason
        // GYRO_CB_SUCCESS is the cheaper way for a timer to stop itself.
        ASSERT_EQ(ticks.calls, 3);
        EXPECT_EQ(ticks.last_status, GYRO_ECANCELED);
    }

    TEST_F(TimerTest, CancellingATimerFromAnotherThreadEndsItEarly) {
        Ticks ticks;
        gyro_request_t token;

        const auto armed = Clock::now();

        // Long enough that only the cancellation can explain an early return.
        ASSERT_EQ(gyro_timer_start(this->loop, 5000, OnTick, &ticks, &token), GYRO_PENDING);

        std::thread other([&] {
            std::this_thread::sleep_for(30ms);

            EXPECT_EQ(gyro_request_cancel(this->loop, token), GYRO_COMPLETED);
        });

        EXPECT_EQ(gyro_run(this->loop), GYRO_COMPLETED);

        other.join();

        ASSERT_EQ(ticks.calls, 1);
        EXPECT_EQ(ticks.last_status, GYRO_ECANCELED);
        EXPECT_LT(Between(armed, ticks.at[0]), 1000ms) << "the cancellation did not reach a sleeping loop";
    }

    TEST_F(TimerTest, ATimerArmedFromAnotherThreadFiresOnTheLoop) {
        Ticks slow, quick;
        quick.last_handle = nullptr;

        // Keeps the loop alive and asleep while the other thread arms its own.
        ASSERT_EQ(gyro_timer_start(this->loop, 200, OnTick, &slow, nullptr), GYRO_PENDING);

        const auto loop_thread = std::this_thread::get_id();
        std::thread::id fired_on;

        struct Probe {
            Ticks *ticks;
            std::thread::id *fired_on;
        } probe{&quick, &fired_on};

        std::thread other([&] {
            std::this_thread::sleep_for(20ms);

            const int rc = gyro_timer_start(this->loop, 10, [](gyro_handle_t *h, const int status, size_t, void *data) {
                auto *p = (Probe *) data;

                *p->fired_on = std::this_thread::get_id();
                p->ticks->calls++;
                p->ticks->last_status = status;
                p->ticks->last_handle = h;

                return GYRO_CB_SUCCESS;
            }, &probe, nullptr);

            EXPECT_EQ(rc, GYRO_PENDING);
        });

        EXPECT_EQ(gyro_run(this->loop), GYRO_COMPLETED);

        other.join();

        EXPECT_EQ(quick.calls, 1);
        EXPECT_EQ(quick.last_status, GYRO_COMPLETED);
        EXPECT_EQ(fired_on, loop_thread) << "a timer's callback belongs to the loop's thread, wherever it was armed";
        EXPECT_EQ(slow.calls, 1);
    }

    TEST_F(TimerTest, ArgumentsTheHeaderRefusesAreRefused) {
        Ticks ticks;

        EXPECT_EQ(gyro_timer_start(this->loop, -1, OnTick, &ticks, nullptr), GYRO_EINVAL);
        EXPECT_EQ(gyro_timer_start(this->loop, 10, nullptr, &ticks, nullptr), GYRO_EINVAL);

        EXPECT_EQ(gyro_timer_start_periodic(this->loop, 0, 0, OnTick, &ticks, nullptr), GYRO_EINVAL);
        EXPECT_EQ(gyro_timer_start_periodic(this->loop, -1, 10, OnTick, &ticks, nullptr), GYRO_EINVAL);
        EXPECT_EQ(gyro_timer_start_periodic(this->loop, 0, 10, nullptr, &ticks, nullptr), GYRO_EINVAL);

        // Refused means nothing was taken: the loop still has no work at all.
        EXPECT_EQ(gyro_run(this->loop), GYRO_COMPLETED);
        EXPECT_EQ(ticks.calls, 0);
    }
} // namespace
