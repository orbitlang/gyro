// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <gtest/gtest.h>

#include <gyro/gyro.h>

namespace {
    /// What a callback saw, so a test can assert on it after the loop returns.
    struct Report {
        int status = GYRO_PENDING;
        size_t transferred = 0;
        int calls = 0;
    };

    /**
     * @brief A loopback pair, connected through the loop rather than behind its
     *        back, so every test starts from a state the library produced.
     *
     * The loop only ever stops from inside a callback: gyro_run() clears the
     * stop flag as it starts, so asking it to stop before it is running has no
     * effect at all.
     */
    class TcpTest : public testing::Test {
    protected:
        gyro_t *loop = nullptr;
        gyro_tcp_t *server = nullptr;
        gyro_tcp_t *conn = nullptr; ///< Server side of the connection.
        gyro_tcp_t *client = nullptr;

        int outstanding = 0;
        int closed = 0;

        static TcpTest *current;

        /// Records the outcome, and stops the loop once the last one is in.
        static gyro_cb_status_t Done(gyro_handle_t *, const int status, const size_t transferred, void *data) {
            if (data != nullptr) {
                auto *report = (Report *) data;

                report->status = status;
                report->transferred = transferred;
                report->calls++;
            }

            TcpTest::current->Arrived();

            return GYRO_CB_SUCCESS;
        }

        /// Records without stopping: for the tests that let the loop notice
        /// on its own that it has run out of work.
        static gyro_cb_status_t Record(gyro_handle_t *, const int status, const size_t transferred, void *data) {
            auto *report = (Report *) data;

            report->status = status;
            report->transferred = transferred;
            report->calls++;

            return GYRO_CB_SUCCESS;
        }

        static void OnClose(gyro_handle_t *) {
            TcpTest::current->closed++;
            TcpTest::current->Arrived();
        }

        void Arrived() {
            if (--this->outstanding == 0)
                gyro_stop(this->loop);
        }

        /// Runs until @p callbacks more callbacks have fired.
        void RunFor(const int callbacks) {
            TcpTest::current = this;
            this->outstanding = callbacks;

            ASSERT_EQ(gyro_run(this->loop), GYRO_STOPPED);
        }

        void SetUp() override {
            TcpTest::current = this;

            this->loop = gyro_new(nullptr);
            ASSERT_NE(this->loop, nullptr);

            this->server = gyro_tcp_new(this->loop);
            this->conn = gyro_tcp_new(this->loop);
            this->client = gyro_tcp_new(this->loop);
            ASSERT_NE(this->server, nullptr);
            ASSERT_NE(this->conn, nullptr);
            ASSERT_NE(this->client, nullptr);

            // Nothing has opened a socket for them yet.
            EXPECT_EQ(gyro_tcp_fileno(this->conn), GYRO_INVALID_SOCKET);

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

            ASSERT_EQ(gyro_tcp_bind(this->server, (sockaddr *) &addr, sizeof(addr), GYRO_TCP_REUSEADDR),
                      GYRO_COMPLETED);
            ASSERT_EQ(gyro_tcp_listen(this->server, 16), GYRO_COMPLETED);

            socklen_t len = sizeof(addr);
            ASSERT_EQ(getsockname((int) gyro_tcp_fileno(this->server), (sockaddr *) &addr, &len), 0);

            Report accepted, connected;

            this->outstanding = 2;

            ASSERT_EQ(gyro_tcp_accept(this->server, this->conn, 0, Done, &accepted, nullptr), GYRO_PENDING);

            const int rc = gyro_tcp_connect(this->client, (sockaddr *) &addr, sizeof(addr), 0,
                                            Done, &connected, nullptr);
            ASSERT_TRUE(rc == GYRO_PENDING || rc == GYRO_COMPLETED);
            if (rc == GYRO_COMPLETED)
                this->outstanding--;

            ASSERT_EQ(gyro_run(this->loop), GYRO_STOPPED);

            ASSERT_EQ(accepted.status, GYRO_COMPLETED);
            ASSERT_NE(gyro_tcp_fileno(this->conn), GYRO_INVALID_SOCKET);
        }

        void TearDown() override {
            TcpTest::current = this;

            gyro_tcp_t *alive[] = {this->client, this->conn, this->server};

            int count = 0;
            for (auto *handle: alive) {
                if (handle != nullptr)
                    count++;
            }

            if (count > 0) {
                this->outstanding = count;

                for (auto *handle: alive) {
                    if (handle != nullptr)
                        gyro_handle_close(GYRO_HANDLE(handle), OnClose);
                }

                // Handles closed earlier with no callback are buried by the
                // same turn, so one run is enough for all of them.
                EXPECT_EQ(gyro_run(this->loop), GYRO_STOPPED);
            }

            EXPECT_EQ(gyro_free(this->loop), GYRO_COMPLETED);
        }
    };

    TcpTest *TcpTest::current = nullptr;

    TEST_F(TcpTest, FreeingALoopWithWorkInItIsRefused) {
        char storage[64];
        gyro_buf_t buf{storage, sizeof(storage)};
        Report report;

        ASSERT_EQ(gyro_tcp_read(this->conn, &buf, 1, 0, Done, &report, nullptr, nullptr), GYRO_PENDING);

        // An operation still owes a callback, so releasing now would drop it.
        EXPECT_EQ(gyro_free(this->loop), GYRO_EBUSY);

        // Refused means untouched: the loop still works, and TearDown will
        // wind it down and free it for real.
        gyro_handle_close(GYRO_HANDLE(this->conn), nullptr);
        RunFor(1);
        this->conn = nullptr;

        EXPECT_EQ(report.status, GYRO_ECANCELED);
    }

    // -----------------------------------------------------------------------
    // Submits from other threads
    //
    // SetUp has already run the loop once, so the loop's thread is this one and
    // anything a std::thread submits takes the queued path. Each test pushes
    // before calling gyro_run() again, so none of them depends on the wakeup.
    // -----------------------------------------------------------------------

    TEST_F(TcpTest, ASubmitFromAnotherThreadIsCarriedOut) {
        char storage[64];
        gyro_buf_t buf{storage, sizeof(storage)};
        Report report;
        int rc = GYRO_EUNKNOWN;

        std::thread other([&] {
            rc = gyro_tcp_read(this->conn, &buf, 1, 0, Done, &report, nullptr, nullptr);
        });
        other.join();

        // It cannot have run yet: the loop that owns the queues is not running.
        EXPECT_EQ(rc, GYRO_PENDING);
        EXPECT_EQ(report.calls, 0);

        char message[] = "hello";
        gyro_buf_t out{message, 5};

        ASSERT_EQ(gyro_tcp_write(this->client, &out, 1, 0, nullptr, nullptr, nullptr, nullptr), GYRO_COMPLETED);

        RunFor(1);

        EXPECT_EQ(report.status, GYRO_COMPLETED);
        EXPECT_EQ(report.transferred, 5u);
        EXPECT_EQ(memcmp(storage, "hello", 5), 0);
    }

    TEST_F(TcpTest, ASubmitFromAnotherThreadWakesASleepingLoop) {
        using namespace std::chrono_literals;

        // A read nothing will ever answer: the loop has work, carries no
        // deadline, and is therefore genuinely asleep in the backend. No
        // callback on it, so it stays out of the accounting.
        char idle[16];
        gyro_buf_t idlebuf{idle, sizeof(idle)};

        ASSERT_EQ(gyro_tcp_read(this->conn, &idlebuf, 1, 0, nullptr, nullptr, nullptr, nullptr), GYRO_PENDING);

        char storage[16];
        gyro_buf_t buf{storage, sizeof(storage)};
        Report report;

        std::thread other([&] {
            std::this_thread::sleep_for(100ms);

            // Nothing will ever satisfy this read either, so the only thing
            // that can end it is its deadline, and the deadline is not set
            // until the loop drains the queue. If the wakeup never arrives the
            // loop stays asleep and no timer is ever armed.
            EXPECT_EQ(gyro_tcp_read(this->client, &buf, 1, 50, Done, &report, nullptr, nullptr), GYRO_PENDING);
        });

        TcpTest::current = this;
        this->outstanding = 1;

        const auto started = std::chrono::steady_clock::now();
        const int rc = gyro_run(this->loop);
        const auto elapsed = std::chrono::steady_clock::now() - started;

        other.join();

        EXPECT_EQ(rc, GYRO_STOPPED);
        EXPECT_EQ(report.status, GYRO_ETIMEDOUT);
        EXPECT_GE(elapsed, 140ms) << "the deadline cannot have been armed before the queue was drained";
    }

    TEST_F(TcpTest, SubmitsFromAnotherThreadKeepTheOrderTheyWereMadeIn) {
        constexpr int kCount = 8;

        // One byte each, so the n-th read can only be satisfied by the n-th
        // byte of the stream: the letters say what order they were served in.
        char storage[kCount] = {};
        gyro_buf_t bufs[kCount];
        Report reports[kCount];

        std::thread other([&] {
            for (int i = 0; i < kCount; i++) {
                bufs[i].base = &storage[i];
                bufs[i].len = 1;

                EXPECT_EQ(gyro_tcp_read(this->conn, &bufs[i], 1, 0, Done, &reports[i], nullptr, nullptr),
                          GYRO_PENDING);
            }
        });
        other.join();

        char message[] = "ABCDEFGH";
        gyro_buf_t out{message, kCount};

        ASSERT_EQ(gyro_tcp_write(this->client, &out, 1, 0, nullptr, nullptr, nullptr, nullptr), GYRO_COMPLETED);

        RunFor(kCount);

        for (int i = 0; i < kCount; i++) {
            EXPECT_EQ(reports[i].status, GYRO_COMPLETED) << "read " << i;
            EXPECT_EQ(storage[i], 'A' + i)
                            << "read " << i << " was served out of turn: the batch is being drained backwards";
        }
    }

    TEST_F(TcpTest, ManyThreadsCanSubmitAtTheSameTime) {
        constexpr int kThreads = 4;
        constexpr int kPerThread = 4;
        constexpr int kCount = kThreads * kPerThread;

        char storage[kCount] = {};
        gyro_buf_t bufs[kCount];
        Report reports[kCount];

        // Every thread pushes onto the same queue at once: a request lost by
        // the compare-exchange never reports, and the loop waits for it for
        // ever rather than failing.
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; t++) {
            threads.emplace_back([&, t] {
                for (int i = 0; i < kPerThread; i++) {
                    const int at = t * kPerThread + i;

                    bufs[at].base = &storage[at];
                    bufs[at].len = 1;

                    EXPECT_EQ(gyro_tcp_read(this->conn, &bufs[at], 1, 0, Done, &reports[at], nullptr, nullptr),
                              GYRO_PENDING);
                }
            });
        }

        for (auto &thread: threads)
            thread.join();

        std::vector<char> message(kCount, 'x');
        gyro_buf_t out{message.data(), kCount};

        ASSERT_EQ(gyro_tcp_write(this->client, &out, 1, 0, nullptr, nullptr, nullptr, nullptr), GYRO_COMPLETED);

        RunFor(kCount);

        for (int i = 0; i < kCount; i++) {
            EXPECT_EQ(reports[i].calls, 1) << "request " << i << " never reported";
            EXPECT_EQ(reports[i].status, GYRO_COMPLETED) << "request " << i;
        }
    }

    TEST_F(TcpTest, RunReturnsAtOnceWhenThereIsNothingToDo) {
        // Every operation from SetUp has reported, so there is no event that
        // could still arrive: waiting for one would be waiting for ever.
        EXPECT_EQ(gyro_run(this->loop), GYRO_COMPLETED);
    }

    TEST_F(TcpTest, RunReturnsOnItsOwnOnceTheWorkIsOver) {
        char storage[64];
        gyro_buf_t buf{storage, sizeof(storage)};
        Report report;

        // Nobody is going to ask the loop to stop.
        ASSERT_EQ(gyro_tcp_read(this->conn, &buf, 1, 0, Record, &report, nullptr, nullptr), GYRO_PENDING);

        char message[] = "hello";
        gyro_buf_t out{message, 5};

        ASSERT_EQ(gyro_tcp_write(this->client, &out, 1, 0, nullptr, nullptr, nullptr, nullptr), GYRO_COMPLETED);

        EXPECT_EQ(gyro_run(this->loop), GYRO_COMPLETED);

        EXPECT_EQ(report.calls, 1);
        EXPECT_EQ(report.status, GYRO_COMPLETED);
        EXPECT_EQ(report.transferred, 5u);
    }

    TEST_F(TcpTest, ReadPostedBeforeAnyDataWaitsForIt) {
        char storage[64];
        gyro_buf_t buf{storage, sizeof(storage)};
        Report report;

        // Nothing has been sent, so the direct attempt cannot succeed and the
        // operation has to be handed to the loop.
        ASSERT_EQ(gyro_tcp_read(this->conn, &buf, 1, 0, Done, &report, nullptr, nullptr), GYRO_PENDING);
        EXPECT_EQ(report.calls, 0);
        EXPECT_EQ(gyro_handle_pending(GYRO_HANDLE(this->conn), GYRO_DIR_IN), 1u);
        EXPECT_EQ(gyro_handle_pending(GYRO_HANDLE(this->conn), GYRO_DIR_OUT), 0u)
                        << "the two directions are driven independently";

        char message[] = "hello";
        gyro_buf_t out{message, 5};
        size_t sent = 0;

        ASSERT_EQ(gyro_tcp_write(this->client, &out, 1, 0, nullptr, nullptr, nullptr, &sent), GYRO_COMPLETED);
        EXPECT_EQ(sent, 5u);

        RunFor(1);

        EXPECT_EQ(report.calls, 1);
        EXPECT_EQ(report.status, GYRO_COMPLETED);
        EXPECT_EQ(report.transferred, 5u);
        EXPECT_EQ(memcmp(storage, "hello", 5), 0);
        EXPECT_EQ(gyro_handle_pending(GYRO_HANDLE(this->conn), GYRO_DIR_IN), 0u);
    }

    TEST_F(TcpTest, ACompletedSubmitDoesNotAlsoCallBack) {
        char message[] = "hello";
        gyro_buf_t out{message, 5};
        size_t sent = 0;

        // A socket with room in its send buffer takes the whole thing at once,
        // which is the case the direct attempt exists for.
        ASSERT_EQ(gyro_tcp_write(this->client, &out, 1, 0, Done, nullptr, nullptr, &sent), GYRO_COMPLETED);
        EXPECT_EQ(sent, 5u);

        char storage[64];
        gyro_buf_t buf{storage, sizeof(storage)};
        Report report;
        size_t transferred = 0;

        const int rc = gyro_tcp_read(this->conn, &buf, 1, 0, Done, &report, nullptr, &transferred);
        if (rc == GYRO_COMPLETED) {
            EXPECT_EQ(transferred, 5u);
            EXPECT_EQ(report.calls, 0) << "a completed submit must not also call back";
            EXPECT_EQ(memcmp(storage, "hello", 5), 0);
        } else {
            ASSERT_EQ(rc, GYRO_PENDING);

            RunFor(1);

            EXPECT_EQ(report.status, GYRO_COMPLETED);
            EXPECT_EQ(report.transferred, 5u);
        }
    }

    TEST_F(TcpTest, WriteReportsOnlyOnceEverythingIsGone) {
        constexpr size_t kSize = 4u << 20; // Far past any socket buffer.

        std::vector<char> out(kSize), in(kSize);
        for (size_t i = 0; i < kSize; i++)
            out[i] = (char) (i * 31 + 7);

        gyro_buf_t sendbuf{out.data(), kSize};
        Report written;

        ASSERT_EQ(gyro_tcp_write(this->client, &sendbuf, 1, 0, Done, &written, nullptr, nullptr), GYRO_PENDING);

        size_t received = 0;
        while (received < kSize) {
            gyro_buf_t recvbuf{in.data() + received, kSize - received};
            Report read;
            size_t got = 0;

            const int rc = gyro_tcp_read(this->conn, &recvbuf, 1, 0, Done, &read, nullptr, &got);
            if (rc == GYRO_COMPLETED) {
                received += got;

                continue;
            }

            ASSERT_EQ(rc, GYRO_PENDING);

            // The write reports through the same callback, so a turn can be
            // spent on it instead: keep going until the read itself is in.
            while (read.calls == 0)
                RunFor(1);

            ASSERT_EQ(read.status, GYRO_COMPLETED);
            received += read.transferred;
        }

        // The write may still have a tail outstanding once the last byte has
        // been read, so give it the turn it needs to report.
        if (written.calls == 0)
            RunFor(1);

        EXPECT_EQ(written.status, GYRO_COMPLETED);
        EXPECT_EQ(written.transferred, kSize);
        EXPECT_EQ(in, out);

        // The caller's array comes back exactly as it went in, even though the
        // write took many syscalls to drain.
        EXPECT_EQ(sendbuf.base, out.data());
        EXPECT_EQ(sendbuf.len, kSize);
    }

    TEST_F(TcpTest, PeerGoingAwayReportsEof) {
        this->outstanding = 1;
        gyro_handle_close(GYRO_HANDLE(this->client), OnClose);
        this->client = nullptr;

        ASSERT_EQ(gyro_run(this->loop), GYRO_STOPPED);

        char storage[64];
        gyro_buf_t buf{storage, sizeof(storage)};
        Report report;

        const int rc = gyro_tcp_read(this->conn, &buf, 1, 0, Done, &report, nullptr, nullptr);
        if (rc == GYRO_PENDING) {
            RunFor(1);

            EXPECT_EQ(report.status, GYRO_EOF);
        } else {
            EXPECT_EQ(rc, GYRO_EOF);
        }
    }

    TEST_F(TcpTest, CancellingAReadReportsItAtOnce) {
        char storage[64];
        gyro_buf_t buf{storage, sizeof(storage)};
        Report report;
        gyro_request_t token;

        // No deadline and a peer that never speaks: without cancellation this
        // request would wait forever.
        ASSERT_EQ(gyro_tcp_read(this->conn, &buf, 1, 0, Done, &report, &token, nullptr), GYRO_PENDING);
        ASSERT_TRUE(GYRO_REQUEST_IS_VALID(token));

        ASSERT_EQ(gyro_request_cancel(this->loop, token), GYRO_COMPLETED);

        RunFor(1);

        EXPECT_EQ(report.calls, 1);
        EXPECT_EQ(report.status, GYRO_ECANCELED);
    }

    TEST_F(TcpTest, ADeadlineGivesUpOnAReadNobodyAnswers) {
        char storage[64];
        gyro_buf_t buf{storage, sizeof(storage)};
        Report report;

        ASSERT_EQ(gyro_tcp_read(this->conn, &buf, 1, 30, Done, &report, nullptr, nullptr), GYRO_PENDING);

        RunFor(1);

        EXPECT_EQ(report.calls, 1);
        EXPECT_EQ(report.status, GYRO_ETIMEDOUT);
    }

    TEST_F(TcpTest, CancellingAnOperationThatAlsoHadADeadlineIsNotATimeout) {
        char storage[64];
        gyro_buf_t buf{storage, sizeof(storage)};
        Report report;
        gyro_request_t token;

        // Generous enough that it cannot expire on its own before the cancel.
        ASSERT_EQ(gyro_tcp_read(this->conn, &buf, 1, 30000, Done, &report, &token, nullptr), GYRO_PENDING);

        ASSERT_EQ(gyro_request_cancel(this->loop, token), GYRO_COMPLETED);

        RunFor(1);

        // Both endings travel the same path, in that the deadline is brought
        // forward to now, so the only thing that keeps them apart is which of
        // the two decided first.
        EXPECT_EQ(report.status, GYRO_ECANCELED);
    }

    TEST_F(TcpTest, ClosingReportsEveryPendingOperationFirst) {
        char storage[64];
        gyro_buf_t buf{storage, sizeof(storage)};
        Report report;

        ASSERT_EQ(gyro_tcp_read(this->conn, &buf, 1, 0, Done, &report, nullptr, nullptr), GYRO_PENDING);

        gyro_handle_close(GYRO_HANDLE(this->conn), OnClose);

        // One for the read and one for the close.
        RunFor(2);
        this->conn = nullptr;

        EXPECT_EQ(report.calls, 1);
        EXPECT_EQ(report.status, GYRO_ECANCELED);
        EXPECT_EQ(this->closed, 1);
    }

    TEST_F(TcpTest, SubmittingOnAClosingHandleFails) {
        char storage[64];
        gyro_buf_t buf{storage, sizeof(storage)};

        gyro_handle_close(GYRO_HANDLE(this->conn), OnClose);

        // The handle is still reachable: gyro_close() only marks it, and the
        // loop is what frees it.
        EXPECT_EQ(gyro_tcp_read(this->conn, &buf, 1, 0, nullptr, nullptr, nullptr, nullptr), GYRO_EBADF);
        EXPECT_EQ(gyro_tcp_write(this->conn, &buf, 1, 0, nullptr, nullptr, nullptr, nullptr), GYRO_EBADF);

        this->outstanding = 1;
        ASSERT_EQ(gyro_run(this->loop), GYRO_STOPPED);
        this->conn = nullptr;

        EXPECT_EQ(this->closed, 1);
    }

    TEST_F(TcpTest, ConnectingToANobodyIsRefused) {
        auto *lonely = gyro_tcp_new(this->loop);
        ASSERT_NE(lonely, nullptr);

        // Port 1 on loopback: privileged, and nothing is listening on it.
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(1);

        Report report;

        const int rc = gyro_tcp_connect(lonely, (sockaddr *) &addr, sizeof(addr), 0, Done, &report, nullptr);
        if (rc == GYRO_PENDING) {
            RunFor(1);

            EXPECT_EQ(report.status, GYRO_ECONNREFUSED);
        } else {
            EXPECT_EQ(rc, GYRO_ECONNREFUSED);
        }

        gyro_handle_close(GYRO_HANDLE(lonely), nullptr);
    }
} // namespace
