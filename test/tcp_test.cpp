// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>

#include <gtest/gtest.h>

#include <gyro/gyro.h>

namespace {
    using namespace std::chrono_literals;

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

        /**
         * @brief Blocks until @p tcp has something to read.
         *
         * Loopback delivers quickly but not synchronously, so a test that
         * wants an inline read to find data has to wait for it rather than
         * assume it. Only waits: the bytes are left for gyro to take.
         */
        static void WaitReadable(gyro_tcp_t *tcp) {
            pollfd pfd{(int) gyro_tcp_fileno(tcp), POLLIN, 0};

            ASSERT_EQ(poll(&pfd, 1, 2000), 1) << "the peer's bytes never arrived";
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
    // Overlap
    //
    // Everything above hands work between threads with a join() in the middle,
    // so the threads never actually run at the same time. These two do overlap,
    // which is the only way a thread sanitiser has anything to look at. They
    // assert on what holds however the interleaving falls: nothing lost, every
    // operation reported once, and the claim counters back to zero.
    // -----------------------------------------------------------------------

    /// Counters a callback on the loop's thread shares with a test asserting
    /// from another.
    struct Tally {
        std::atomic<int> calls{0};
        std::atomic<size_t> bytes{0};

        void Reset() {
            this->calls.store(0);
            this->bytes.store(0);
        }
    };

    Tally tally;

    gyro_cb_status_t Count(gyro_handle_t *, int, const size_t transferred, void *) {
        tally.bytes.fetch_add(transferred);
        tally.calls.fetch_add(1);

        return GYRO_CB_SUCCESS;
    }

    TEST_F(TcpTest, InlineWritesFromManyThreadsOverlapWithARunningLoop) {
        constexpr int kThreads = 4;
        constexpr int kEach = 16;
        constexpr size_t kBytes = 4;
        constexpr int kTotal = kThreads * kEach;

        tally.Reset();

        // Small enough that the whole lot fits in the socket buffer, so no
        // reader is needed and nothing blocks on the peer.
        std::vector storage(kTotal * kBytes, 'x');
        std::vector<gyro_buf_t> bufs(kTotal);
        for (int i = 0; i < kTotal; i++)
            bufs[i] = gyro_buf_t{&storage[i * kBytes], kBytes};

        std::atomic running{kThreads};
        std::atomic inline_calls{0};
        std::atomic<size_t> inline_bytes{0};

        std::vector<std::thread> threads;
        for (int i = 0; i < kThreads; i++) {
            threads.emplace_back([&, i] {
                for (int j = 0; j < kEach; j++) {
                    size_t sent = 0;

                    // Four threads on one direction: at most one of them is
                    // inside the syscall, the rest queue. Which is which is
                    // undefined, and the test does not care.
                    const int rc = gyro_tcp_write(this->client, &bufs[i * kEach + j], 1, 0,
                                                  Count, nullptr, nullptr, &sent);
                    if (rc == GYRO_COMPLETED) {
                        inline_calls.fetch_add(1);
                        inline_bytes.fetch_add(sent);
                    }
                }

                running.fetch_sub(1);
            });
        }

        // Runs alongside them, coming back whenever it runs dry and going
        // straight back in. The point is the overlap, not the timing.
        while (running.load() > 0)
            gyro_run(this->loop);

        for (auto &thread: threads)
            thread.join();

        EXPECT_EQ(gyro_run(this->loop), GYRO_COMPLETED);

        // Reported exactly once each, whether here or on the loop.
        EXPECT_EQ(inline_calls.load() + tally.calls.load(), kTotal);
        EXPECT_EQ(inline_bytes.load() + tally.bytes.load(), kTotal * kBytes);

        // The one that would catch a claim taken and not given back.
        EXPECT_EQ(gyro_handle_pending(GYRO_HANDLE(this->client), GYRO_DIR_OUT), 0u);
    }

    TEST_F(TcpTest, CancellingOverlapsWithAnotherThreadTakingSlotsFromTheStore) {
        constexpr int kThreads = 4;
        constexpr int kEach = 64;
        constexpr int kTotal = kThreads * kEach;
        constexpr long long kDeadline = 50;

        tally.Reset();

        char storage[kTotal] = {};
        std::vector<gyro_buf_t> bufs(kTotal);
        for (int i = 0; i < kTotal; i++)
            bufs[i] = gyro_buf_t{&storage[i], 1};

        std::atomic<int> running{kThreads};

        std::vector<std::thread> threads;
        for (int i = 0; i < kThreads; i++) {
            threads.emplace_back([&, i] {
                for (int j = 0; j < kEach; j++) {
                    // Nobody is writing, so each of these takes a slot out of
                    // the store and waits for its deadline.
                    EXPECT_EQ(gyro_tcp_read(this->conn, &bufs[i * kEach + j], 1, kDeadline,
                                            Count, nullptr, nullptr, nullptr),
                              GYRO_PENDING);
                }

                running.fetch_sub(1);
            });
        }

        // Resolving a token walks the store's page directory, which the submits
        // above are growing from their own threads. Cancelling is the loop
        // thread's alone, and this is it: gyro_run() is not running, but the id
        // it recorded is still this one.
        const gyro_request_t stale = gyro_request_invalid();

        int cancels = 0;
        while (running.load() > 0) {
            gyro_request_cancel(this->loop, stale);
            cancels++;
        }

        for (auto &thread: threads)
            thread.join();

        EXPECT_GT(cancels, 0) << "the submits finished before a single cancel got in";

        // Their deadlines are what ends them, so one turn is enough.
        while (tally.calls.load() < kTotal)
            gyro_run(this->loop);

        EXPECT_EQ(tally.calls.load(), kTotal);
        EXPECT_EQ(gyro_handle_pending(GYRO_HANDLE(this->conn), GYRO_DIR_IN), 0u);
    }

    // -----------------------------------------------------------------------
    // The inline claim
    //
    // An operation may be carried out on whatever thread asked for it, but only
    // one at a time per direction, and only while nothing else is outstanding.
    // What enforces that is a claim taken on the handle, which the operation
    // gives back when it ends here or hands to the request when it is queued.
    // -----------------------------------------------------------------------

    TEST_F(TcpTest, AWriteFromAnotherThreadIsCarriedOutOnThatThread) {
        char message[] = "hello";
        gyro_buf_t out{message, 5};

        int rc = GYRO_EUNKNOWN;
        size_t sent = 0;

        std::thread other([&] {
            rc = gyro_tcp_write(this->client, &out, 1, 0, nullptr, nullptr, nullptr, &sent);
        });
        other.join();

        // The loop is not running and never sees this one: a socket with room
        // in its send buffer is served by the thread that asked.
        EXPECT_EQ(rc, GYRO_COMPLETED);
        EXPECT_EQ(sent, 5u);

        // Nothing was left behind, so the next operation can go inline too.
        EXPECT_EQ(gyro_handle_pending(GYRO_HANDLE(this->client), GYRO_DIR_OUT), 0u);
    }

    TEST_F(TcpTest, AReadFromAnotherThreadIsCarriedOutOnThatThread) {
        char message[] = "hello";
        gyro_buf_t out{message, 5};
        ASSERT_EQ(gyro_tcp_write(this->client, &out, 1, 0, nullptr, nullptr, nullptr, nullptr), GYRO_COMPLETED);

        WaitReadable(this->conn);

        char storage[64] = {};
        gyro_buf_t in{storage, sizeof(storage)};

        int rc = GYRO_EUNKNOWN;
        size_t got = 0;

        std::thread other([&] {
            rc = gyro_tcp_read(this->conn, &in, 1, 0, nullptr, nullptr, nullptr, &got);
        });
        other.join();

        EXPECT_EQ(rc, GYRO_COMPLETED);
        EXPECT_EQ(got, 5u);
        EXPECT_EQ(memcmp(storage, "hello", 5), 0);

        EXPECT_EQ(gyro_handle_pending(GYRO_HANDLE(this->conn), GYRO_DIR_IN), 0u);
    }

    TEST_F(TcpTest, AnOperationChainedFromACallbackCanGoInline) {
        struct Chain {
            gyro_tcp_t *conn;
            char first = 0, second = 0;
            int second_rc = GYRO_EUNKNOWN;
            size_t second_got = 0;
        } chain{this->conn};

        gyro_buf_t one{&chain.first, 1};

        // Queued: nothing to read yet, so this one reports through its callback.
        const auto cb = [](gyro_handle_t *, int, size_t, void *data) {
            auto *c = (Chain *) data;
            gyro_buf_t two{&c->second, 1};

            // The commonest thing a callback does is start the next operation.
            // The one reporting here is finished in every sense by now, so the
            // direction is free and the byte already waiting is taken on the
            // spot. Were the claim still held until the callback returned, this
            // would be queued for no reason and the fast path lost exactly
            // where it is used most.
            c->second_rc = gyro_tcp_read(c->conn, &two, 1, 0, nullptr, nullptr, nullptr, &c->second_got);

            // Done() records nothing for a null report and stops the loop.
            return Done(nullptr, 0, 0, nullptr);
        };

        ASSERT_EQ(gyro_tcp_read(this->conn, &one, 1, 0, cb, &chain, nullptr, nullptr), GYRO_PENDING);

        char message[] = "AB";
        gyro_buf_t out{message, 2};
        ASSERT_EQ(gyro_tcp_write(this->client, &out, 1, 0, nullptr, nullptr, nullptr, nullptr), GYRO_COMPLETED);

        RunFor(1);

        EXPECT_EQ(chain.first, 'A');
        EXPECT_EQ(chain.second_rc, GYRO_COMPLETED) << "the chained read was queued behind the operation reporting to it";
        EXPECT_EQ(chain.second_got, 1u);
        EXPECT_EQ(chain.second, 'B');
    }

    TEST_F(TcpTest, AnOperationThatFailsAtOnceStillGivesTheClaimBack) {
        gyro_handle_close(GYRO_HANDLE(this->client), OnClose);
        RunFor(1);
        this->client = nullptr;

        char storage[64];
        gyro_buf_t in{storage, sizeof(storage)};
        Report report;

        // Closing the peer does not put the FIN on this socket, it only sends
        // it, so the first read is whichever of the two the timing allows.
        // Settling it here is what makes the ones below deterministic.
        const int rc = gyro_tcp_read(this->conn, &in, 1, 0, Done, &report, nullptr, nullptr);
        if (rc == GYRO_PENDING) {
            RunFor(1);
            ASSERT_EQ(report.status, GYRO_EOF);
        } else {
            ASSERT_EQ(rc, GYRO_EOF);
        }

        // The end of the stream has been seen now, so this one is decided on
        // the spot and no request is ever created. The claim taken on the way
        // in has nobody to hand it to.
        EXPECT_EQ(gyro_tcp_read(this->conn, &in, 1, 0, nullptr, nullptr, nullptr, nullptr), GYRO_EOF);
        EXPECT_EQ(gyro_handle_pending(GYRO_HANDLE(this->conn), GYRO_DIR_IN), 0u);

        // A leaked claim is invisible until something stops going inline, so
        // the assertion that matters is that the direction still works.
        EXPECT_EQ(gyro_tcp_read(this->conn, &in, 1, 0, nullptr, nullptr, nullptr, nullptr), GYRO_EOF);
    }

    TEST_F(TcpTest, AnOperationOutstandingKeepsTheNextOneOutOfTheFastPath) {
        char first = 0, second = 0;
        gyro_buf_t one{&first, 1};
        gyro_buf_t two{&second, 1};
        Report reports[2];

        // Queued while there is nothing to read.
        ASSERT_EQ(gyro_tcp_read(this->conn, &one, 1, 0, Done, &reports[0], nullptr, nullptr), GYRO_PENDING);

        char message[] = "AB";
        gyro_buf_t out{message, 2};
        ASSERT_EQ(gyro_tcp_write(this->client, &out, 1, 0, nullptr, nullptr, nullptr, nullptr), GYRO_COMPLETED);

        // Both bytes are in the kernel now, so this read could be satisfied on
        // the spot. It must not be: taking them here would hand the second
        // byte to the first read and the first byte to nobody.
        int rc = GYRO_EUNKNOWN;
        std::thread other([&] {
            rc = gyro_tcp_read(this->conn, &two, 1, 0, Done, &reports[1], nullptr, nullptr);
        });
        other.join();

        EXPECT_EQ(rc, GYRO_PENDING);

        RunFor(2);

        EXPECT_EQ(first, 'A');
        EXPECT_EQ(second, 'B');
        EXPECT_EQ(reports[0].status, GYRO_COMPLETED);
        EXPECT_EQ(reports[1].status, GYRO_COMPLETED);
    }

    TEST_F(TcpTest, APartialWriteKeepsTheDirectionClaimedUntilItIsQueued) {
        // A send buffer small enough that one write cannot fit in it, and a
        // peer that never reads, so the write is bound to go short.
        constexpr int kSmall = 4096;
        ASSERT_EQ(setsockopt((int) gyro_tcp_fileno(this->client), SOL_SOCKET, SO_SNDBUF, &kSmall, sizeof(kSmall)), 0);
        ASSERT_EQ(setsockopt((int) gyro_tcp_fileno(this->conn), SOL_SOCKET, SO_RCVBUF, &kSmall, sizeof(kSmall)), 0);

        std::vector<char> bulk(4u << 20, 'A');
        gyro_buf_t big{bulk.data(), bulk.size()};

        char tail[] = "ZZZZ";
        gyro_buf_t small{tail, 4};

        Report first, second;

        // Goes out in part and the remainder is queued, which is the whole
        // point: between here and the loop picking it up, the handle's own
        // queue is still empty.
        ASSERT_EQ(gyro_tcp_write(this->client, &big, 1, 0, Done, &first, nullptr, nullptr), GYRO_PENDING);

        // Counting queue entries would say zero here, this write would go
        // inline, and its bytes would land in the middle of the one above.
        // One thread, two calls in a row, and nobody misused anything.
        ASSERT_EQ(gyro_tcp_write(this->client, &small, 1, 0, Done, &second, nullptr, nullptr), GYRO_PENDING);

        EXPECT_EQ(gyro_handle_pending(GYRO_HANDLE(this->client), GYRO_DIR_OUT), 2u)
                        << "the remainder of the first write is invisible to the direction";

        // Neither will ever finish: nobody is draining the peer. Closing
        // cancels both, which is all this test needs.
        gyro_handle_close(GYRO_HANDLE(this->client), OnClose);
        RunFor(3);
        this->client = nullptr;

        EXPECT_EQ(first.status, GYRO_ECANCELED);
        EXPECT_EQ(second.status, GYRO_ECANCELED);
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

    // -----------------------------------------------------------------------
    // Cancellation from anywhere
    //
    // A cancellation is never carried out where it is asked for: the token is
    // handed to the loop, which resolves it in its own thread, on its next turn.
    // That is what makes it safe from any thread, and what fixes its place in
    // line behind the submit it names.
    // -----------------------------------------------------------------------

    TEST_F(TcpTest, ACancelFromAnotherThreadWakesTheLoopAndReports) {
        char storage[64];
        gyro_buf_t buf{storage, sizeof(storage)};
        Report report;
        gyro_request_t token;

        // No deadline, no peer: only a cancellation can end this, and the loop
        // will be asleep with nothing to wake it but that.
        ASSERT_EQ(gyro_tcp_read(this->conn, &buf, 1, 0, Done, &report, &token, nullptr), GYRO_PENDING);

        std::thread other([&] {
            std::this_thread::sleep_for(50ms);

            EXPECT_EQ(gyro_request_cancel(this->loop, token), GYRO_COMPLETED);
        });

        // Coming back at all is the assertion: the carrier has to reach a loop
        // that is blocked with no deadline of its own.
        RunFor(1);

        other.join();

        EXPECT_EQ(report.calls, 1);
        EXPECT_EQ(report.status, GYRO_ECANCELED);
    }

    TEST_F(TcpTest, ACancelReachesTheLoopBehindTheSubmitItNames) {
        char storage[64];
        gyro_buf_t buf{storage, sizeof(storage)};
        Report report;
        gyro_request_t token;

        // Submitted from elsewhere and never drained: the operation is on the
        // wakeup queue and in no heap and no handle queue.
        std::thread other([&] {
            EXPECT_EQ(gyro_tcp_read(this->conn, &buf, 1, 0, Done, &report, &token, nullptr), GYRO_PENDING);
        });
        other.join();
        ASSERT_TRUE(GYRO_REQUEST_IS_VALID(token));

        // Cancelled from the loop's thread before the loop has seen the submit.
        // Acted on in place, this would put the request into the heap now and
        // again when its submit is drained, and the heap would be corrupted.
        // Posted, it queues behind the submit and finds it in place.
        ASSERT_EQ(gyro_request_cancel(this->loop, token), GYRO_COMPLETED);

        RunFor(1);

        EXPECT_EQ(report.calls, 1);
        EXPECT_EQ(report.status, GYRO_ECANCELED);
    }

    TEST_F(TcpTest, ACancelFromInsideTheOperationsOwnCallbackDoesNothing) {
        struct Self {
            gyro_t *loop = nullptr;
            gyro_request_t token = gyro_request_invalid();
            int status = GYRO_PENDING;
            int calls = 0;
        } self;

        self.loop = this->loop;

        // Cancels itself from within its own report. By then the slot has
        // already gone back to the store and the token is stale, so what the
        // carrier finds when it is drained is nothing, and it does nothing.
        const auto cb = [](gyro_handle_t *, const int status, size_t, void *data) {
            auto *s = (Self *) data;

            s->status = status;
            s->calls++;

            EXPECT_EQ(gyro_request_cancel(s->loop, s->token), GYRO_COMPLETED);

            return GYRO_CB_SUCCESS;
        };

        char storage[64];
        gyro_buf_t buf{storage, sizeof(storage)};

        ASSERT_EQ(gyro_tcp_read(this->conn, &buf, 1, 30, cb, &self, &self.token, nullptr), GYRO_PENDING);

        // Ends on its own through the deadline; the loop then drains the
        // carrier the callback posted, finds nothing, and runs dry.
        EXPECT_EQ(gyro_run(this->loop), GYRO_COMPLETED);

        EXPECT_EQ(self.calls, 1);
        EXPECT_EQ(self.status, GYRO_ETIMEDOUT);

        // The same token again, from another thread this time: stale, and
        // still a success that does nothing, which is the whole contract.
        std::thread other([&] {
            EXPECT_EQ(gyro_request_cancel(this->loop, self.token), GYRO_COMPLETED);
        });
        other.join();

        EXPECT_EQ(gyro_run(this->loop), GYRO_COMPLETED);
        EXPECT_EQ(self.calls, 1);
    }

    TEST_F(TcpTest, ACancelOnAnInvalidTokenPostsNothing) {
        // Nothing is handed to the loop for a token that names nothing, so a
        // loop with no work stays a loop with no work. Were a carrier posted,
        // the loop would have something to drain and this run would not be the
        // immediate return an idle loop makes.
        EXPECT_EQ(gyro_request_cancel(this->loop, gyro_request_invalid()), GYRO_COMPLETED);

        const auto started = std::chrono::steady_clock::now();
        EXPECT_EQ(gyro_run(this->loop), GYRO_COMPLETED);
        const auto elapsed = std::chrono::steady_clock::now() - started;

        EXPECT_LT(elapsed, 10ms);
    }

    // -----------------------------------------------------------------------
    // Closing from anywhere
    //
    // Like a cancellation, a close is posted rather than performed: the state
    // flips in the call, so nothing new gets in, and the walk over the queues
    // happens on the loop's thread. Being on the same queue as every submit
    // puts it behind the ones already on their way, which is what lets it see
    // them.
    // -----------------------------------------------------------------------

    TEST_F(TcpTest, ClosingAfterASubmitFromAnotherThreadCancelsIt) {
        char storage[64];
        gyro_buf_t buf{storage, sizeof(storage)};
        Report report;

        // Submitted from elsewhere, returned, and never drained: the read is
        // on the wakeup queue and in neither of the handle's own queues.
        std::thread other([&] {
            EXPECT_EQ(gyro_tcp_read(this->conn, &buf, 1, 0, Done, &report, nullptr, nullptr), GYRO_PENDING);
        });
        other.join();

        // Sequential, not concurrent: the submit had returned before this was
        // called, so whoever closes is entitled to see it cancelled. A close
        // acting on the spot would walk two empty queues and miss it, and the
        // read would then land on a closing handle with nobody left to end it.
        ASSERT_EQ(gyro_handle_close(GYRO_HANDLE(this->conn), OnClose), GYRO_COMPLETED);
        this->conn = nullptr;

        RunFor(2);

        EXPECT_EQ(report.calls, 1);
        EXPECT_EQ(report.status, GYRO_ECANCELED);
        EXPECT_EQ(this->closed, 1);
    }

    TEST_F(TcpTest, ClosingFromAnotherThreadWakesTheLoopAndBuriesTheHandle) {
        char storage[64];
        gyro_buf_t buf{storage, sizeof(storage)};
        Report report;

        // The loop will be asleep on this with no deadline: only the close can
        // bring it back.
        ASSERT_EQ(gyro_tcp_read(this->conn, &buf, 1, 0, Done, &report, nullptr, nullptr), GYRO_PENDING);

        std::thread other([&] {
            std::this_thread::sleep_for(50ms);

            EXPECT_EQ(gyro_handle_close(GYRO_HANDLE(this->conn), OnClose), GYRO_COMPLETED);
        });

        RunFor(2);

        other.join();
        this->conn = nullptr;

        EXPECT_EQ(report.status, GYRO_ECANCELED);
        EXPECT_EQ(this->closed, 1);
    }

    TEST_F(TcpTest, TwoThreadsClosingTheSameHandleReportItOnce) {
        constexpr int kHandles = 64;

        static std::atomic<int> buried;
        buried.store(0);

        // Socketless handles: closing is about the handle, not the descriptor.
        std::vector<gyro_tcp_t *> handles(kHandles);
        for (auto &h: handles) {
            h = gyro_tcp_new(this->loop);
            ASSERT_NE(h, nullptr);
        }

        const auto on_close = [](gyro_handle_t *) { buried.fetch_add(1); };

        // Both threads close every handle, released together so that they
        // meet on as many as the scheduler allows. Exactly one of them wins
        // each; the other must find it already closing and do nothing.
        std::atomic<int> go{0};
        const auto closer = [&] {
            go.fetch_add(1);
            while (go.load() < 2) {
            }

            for (auto *h: handles)
                EXPECT_EQ(gyro_handle_close(GYRO_HANDLE(h), on_close), GYRO_COMPLETED);
        };

        std::thread a(closer), b(closer);
        a.join();
        b.join();

        // Nothing here stops the loop, so it runs dry on its own once every
        // handle has been buried. A handle entered twice into the closing list
        // would be freed twice and never get this far.
        EXPECT_EQ(gyro_run(this->loop), GYRO_COMPLETED);

        EXPECT_EQ(buried.load(), kHandles);
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
