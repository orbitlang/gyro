// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <atomic>
#include <chrono>
#include <climits>
#include <cstring>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

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
     * @brief Two sockets bound to loopback, each knowing the other's address.
     *
     * `a` is the one tests usually receive on and `b` the one they send from,
     * but nothing ties them to those roles: a UDP socket does both. Every
     * address comes from getsockname(), so the ports are the ones the kernel
     * picked and no two runs collide.
     *
     * As in the TCP suite, the loop only ever stops from inside a callback:
     * gyro_run() clears the stop flag as it starts.
     */
    class UdpTest : public testing::Test {
    protected:
        gyro_t *loop = nullptr;
        gyro_udp_t *a = nullptr;
        gyro_udp_t *b = nullptr;

        sockaddr_storage addr_a{};
        sockaddr_storage addr_b{};

        int outstanding = 0;
        int closed = 0;

        static UdpTest *current;

        /// Records the outcome, and stops the loop once the last one is in.
        static gyro_cb_status_t Done(gyro_handle_t *, const int status, const size_t transferred, void *data) {
            if (data != nullptr) {
                auto *report = (Report *) data;

                report->status = status;
                report->transferred = transferred;
                report->calls++;
            }

            UdpTest::current->Arrived();

            return GYRO_CB_SUCCESS;
        }

        static void OnClose(gyro_handle_t *) {
            UdpTest::current->closed++;
            UdpTest::current->Arrived();
        }

        void Arrived() {
            if (--this->outstanding == 0)
                gyro_stop(this->loop);
        }

        /// Runs until @p callbacks more callbacks have fired.
        void RunFor(const int callbacks) {
            UdpTest::current = this;
            this->outstanding = callbacks;

            ASSERT_EQ(gyro_run(this->loop), GYRO_STOPPED);
        }

        /**
         * @brief Blocks until @p udp has a datagram waiting.
         *
         * Loopback delivers quickly but a test that wants an inline receive to
         * find something has to know rather than assume. Only waits: the
         * datagram is left for gyro to take.
         */
        static void WaitReadable(const gyro_udp_t *udp) {
            pollfd pfd{(int) gyro_udp_fileno(udp), POLLIN, 0};

            ASSERT_EQ(poll(&pfd, 1, 2000), 1) << "the datagram never arrived";
        }

        static sockaddr_storage AddressOf(const gyro_udp_t *udp) {
            sockaddr_storage addr{};
            socklen_t len = sizeof(addr);

            EXPECT_EQ(getsockname((int) gyro_udp_fileno(udp), (sockaddr *) &addr, &len), 0);

            return addr;
        }

        static in_port_t PortOf(const sockaddr_storage &addr) {
            return ((const sockaddr_in *) &addr)->sin_port;
        }

        /// A fresh socket on loopback with a port of its own.
        gyro_udp_t *NewBound() const {
            auto *udp = gyro_udp_new(this->loop);
            EXPECT_NE(udp, nullptr);

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

            EXPECT_EQ(gyro_udp_bind(udp, (sockaddr *) &addr, 0), GYRO_COMPLETED);

            return udp;
        }

        /// Closes a handle the test made itself, and waits until it is gone.
        void CloseAndWait(gyro_udp_t *udp) {
            gyro_handle_close(GYRO_HANDLE(udp), OnClose);
            RunFor(1);
        }

        void SetUp() override {
            UdpTest::current = this;

            this->loop = gyro_new(nullptr);
            ASSERT_NE(this->loop, nullptr);

            this->a = NewBound();
            this->b = NewBound();
            ASSERT_NE(this->a, nullptr);
            ASSERT_NE(this->b, nullptr);

            this->addr_a = AddressOf(this->a);
            this->addr_b = AddressOf(this->b);
        }

        void TearDown() override {
            UdpTest::current = this;

            gyro_udp_t *alive[] = {this->a, this->b};

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

                EXPECT_EQ(gyro_run(this->loop), GYRO_STOPPED);
            }

            EXPECT_EQ(gyro_free(this->loop), GYRO_COMPLETED);
        }
    };

    UdpTest *UdpTest::current = nullptr;

    // -----------------------------------------------------------------------
    // Datagrams
    //
    // What makes UDP not a stream: one receive is one datagram, whole, and
    // what does not fit is gone rather than left for the next receive.
    // -----------------------------------------------------------------------

    TEST_F(UdpTest, ADatagramAlreadyWaitingIsTakenInline) {
        char message[] = "hello";
        gyro_buf_t out{message, 5};
        size_t sent = 0;

        ASSERT_EQ(gyro_udp_send(this->b, &out, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, &sent),
                  GYRO_COMPLETED);
        EXPECT_EQ(sent, 5u);

        WaitReadable(this->a);

        char storage[64] = {};
        gyro_buf_t in{storage, sizeof(storage)};
        sockaddr_storage from{};
        size_t got = 0;

        EXPECT_EQ(gyro_udp_recv(this->a, &in, 1, &from, 0, nullptr, nullptr, nullptr, &got), GYRO_COMPLETED);
        EXPECT_EQ(got, 5u);
        EXPECT_EQ(memcmp(storage, "hello", 5), 0);

        // The sender is reported with its family, which is all it takes to
        // know how long the address is.
        EXPECT_EQ(from.ss_family, AF_INET);
        EXPECT_EQ(PortOf(from), PortOf(this->addr_b));
    }

    TEST_F(UdpTest, AReceivePostedBeforeAnyDatagramWaitsForIt) {
        char storage[64] = {};
        gyro_buf_t in{storage, sizeof(storage)};
        sockaddr_storage from{};
        Report report;

        ASSERT_EQ(gyro_udp_recv(this->a, &in, 1, &from, 0, Done, &report, nullptr, nullptr), GYRO_PENDING);

        char message[] = "late";
        gyro_buf_t out{message, 4};
        ASSERT_EQ(gyro_udp_send(this->b, &out, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, nullptr),
                  GYRO_COMPLETED);

        RunFor(1);

        EXPECT_EQ(report.calls, 1);
        EXPECT_EQ(report.status, GYRO_COMPLETED);
        EXPECT_EQ(report.transferred, 4u);
        EXPECT_EQ(memcmp(storage, "late", 4), 0);

        // Written when the datagram arrived, long after the call returned.
        EXPECT_EQ(from.ss_family, AF_INET);
        EXPECT_EQ(PortOf(from), PortOf(this->addr_b));
    }

    TEST_F(UdpTest, AReceiveTakesOneDatagramAndNeverMore) {
        char first[] = "ab", second[] = "cde";
        gyro_buf_t one{first, 2}, two{second, 3};

        ASSERT_EQ(gyro_udp_send(this->b, &one, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, nullptr),
                  GYRO_COMPLETED);
        ASSERT_EQ(gyro_udp_send(this->b, &two, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, nullptr),
                  GYRO_COMPLETED);

        // Room for both, and still only the first: a TCP read would have
        // taken all five bytes.
        char storage[64] = {};
        gyro_buf_t in{storage, sizeof(storage)};
        size_t got = 0;

        WaitReadable(this->a);
        EXPECT_EQ(gyro_udp_recv(this->a, &in, 1, nullptr, 0, nullptr, nullptr, nullptr, &got), GYRO_COMPLETED);
        EXPECT_EQ(got, 2u);
        EXPECT_EQ(memcmp(storage, "ab", 2), 0);

        WaitReadable(this->a);
        EXPECT_EQ(gyro_udp_recv(this->a, &in, 1, nullptr, 0, nullptr, nullptr, nullptr, &got), GYRO_COMPLETED);
        EXPECT_EQ(got, 3u);
        EXPECT_EQ(memcmp(storage, "cde", 3), 0);
    }

    TEST_F(UdpTest, RegionsAreGatheredIntoOneDatagramAndScatteredBack) {
        char head[] = "he", tail[] = "llo";
        gyro_buf_t out[2] = {{head, 2}, {tail, 3}};
        size_t sent = 0;

        ASSERT_EQ(gyro_udp_send(this->b, out, 2, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, &sent),
                  GYRO_COMPLETED);
        EXPECT_EQ(sent, 5u);

        // Split differently on the way in: the first region fills, and the
        // rest of the same datagram spills into the next.
        char x[3] = {}, y[16] = {};
        gyro_buf_t in[2] = {{x, sizeof(x)}, {y, sizeof(y)}};
        size_t got = 0;

        WaitReadable(this->a);
        EXPECT_EQ(gyro_udp_recv(this->a, in, 2, nullptr, 0, nullptr, nullptr, nullptr, &got), GYRO_COMPLETED);
        EXPECT_EQ(got, 5u);
        EXPECT_EQ(memcmp(x, "hel", 3), 0);
        EXPECT_EQ(memcmp(y, "lo", 2), 0);
    }

    TEST_F(UdpTest, ADatagramTooBigForTheRegionsIsTruncatedAndSaysSo) {
        char message[] = "hello";
        gyro_buf_t out{message, 5};
        ASSERT_EQ(gyro_udp_send(this->b, &out, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, nullptr),
                  GYRO_COMPLETED);

        char small[3] = {};
        gyro_buf_t in{small, sizeof(small)};
        size_t got = 0;

        WaitReadable(this->a);

        // An outcome, not a short read: the count is what fit.
        EXPECT_EQ(gyro_udp_recv(this->a, &in, 1, nullptr, 0, nullptr, nullptr, nullptr, &got), GYRO_EMSGSIZE);
        EXPECT_EQ(got, 3u);
        EXPECT_EQ(memcmp(small, "hel", 3), 0);
    }

    TEST_F(UdpTest, WhatDidNotFitIsGoneRatherThanLeftForTheNextReceive) {
        char first[] = "hello", second[] = "XY";
        gyro_buf_t one{first, 5}, two{second, 2};

        ASSERT_EQ(gyro_udp_send(this->b, &one, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, nullptr),
                  GYRO_COMPLETED);
        ASSERT_EQ(gyro_udp_send(this->b, &two, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, nullptr),
                  GYRO_COMPLETED);

        char small[3] = {};
        gyro_buf_t in{small, sizeof(small)};
        size_t got = 0;

        WaitReadable(this->a);
        ASSERT_EQ(gyro_udp_recv(this->a, &in, 1, nullptr, 0, nullptr, nullptr, nullptr, &got), GYRO_EMSGSIZE);

        // "lo" is not what comes next. The next datagram is.
        WaitReadable(this->a);
        EXPECT_EQ(gyro_udp_recv(this->a, &in, 1, nullptr, 0, nullptr, nullptr, nullptr, &got), GYRO_COMPLETED);
        EXPECT_EQ(got, 2u);
        EXPECT_EQ(memcmp(small, "XY", 2), 0);
    }

    TEST_F(UdpTest, ATruncationReportedLaterCountsWhatFit) {
        char small[3] = {};
        gyro_buf_t in{small, sizeof(small)};
        Report report;

        ASSERT_EQ(gyro_udp_recv(this->a, &in, 1, nullptr, 0, Done, &report, nullptr, nullptr), GYRO_PENDING);

        char message[] = "hello";
        gyro_buf_t out{message, 5};
        ASSERT_EQ(gyro_udp_send(this->b, &out, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, nullptr),
                  GYRO_COMPLETED);

        RunFor(1);

        EXPECT_EQ(report.status, GYRO_EMSGSIZE);
        EXPECT_EQ(report.transferred, 3u);
        EXPECT_EQ(memcmp(small, "hel", 3), 0);
    }

    TEST_F(UdpTest, AnEmptyDatagramIsADatagramAndNotAnEnd) {
        char nothing = 0;
        gyro_buf_t out{&nothing, 0};
        size_t sent = 99;

        ASSERT_EQ(gyro_udp_send(this->b, &out, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, &sent),
                  GYRO_COMPLETED);
        EXPECT_EQ(sent, 0u);

        char storage[8];
        gyro_buf_t in{storage, sizeof(storage)};
        size_t got = 99;

        // Never GYRO_EOF: there is no end to a UDP socket, and zero bytes is
        // simply how long this one was.
        WaitReadable(this->a);
        EXPECT_EQ(gyro_udp_recv(this->a, &in, 1, nullptr, 0, nullptr, nullptr, nullptr, &got), GYRO_COMPLETED);
        EXPECT_EQ(got, 0u);
    }

    TEST_F(UdpTest, ADatagramLargerThanUdpCarriesIsRefused) {
        // Past the 65507 bytes an IPv4 datagram can hold at all, so the answer
        // is the same on every system whatever its own limit is.
        std::vector<char> big(70000, 'x');
        gyro_buf_t out{big.data(), big.size()};
        size_t sent = 99;

        EXPECT_EQ(gyro_udp_send(this->b, &out, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, &sent),
                  GYRO_EMSGSIZE);
        EXPECT_EQ(sent, 0u) << "a datagram leaves whole or not at all";
    }

    TEST_F(UdpTest, ReceivesAreServedInTheOrderTheyWereSubmitted) {
        char first = 0, second = 0;
        gyro_buf_t one{&first, 1}, two{&second, 1};
        Report reports[2];

        ASSERT_EQ(gyro_udp_recv(this->a, &one, 1, nullptr, 0, Done, &reports[0], nullptr, nullptr), GYRO_PENDING);
        ASSERT_EQ(gyro_udp_recv(this->a, &two, 1, nullptr, 0, Done, &reports[1], nullptr, nullptr), GYRO_PENDING);

        char m1 = '1', m2 = '2';
        gyro_buf_t out1{&m1, 1}, out2{&m2, 1};
        ASSERT_EQ(gyro_udp_send(this->b, &out1, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, nullptr),
                  GYRO_COMPLETED);
        ASSERT_EQ(gyro_udp_send(this->b, &out2, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, nullptr),
                  GYRO_COMPLETED);

        RunFor(2);

        EXPECT_EQ(first, '1');
        EXPECT_EQ(second, '2');
        EXPECT_EQ(reports[0].status, GYRO_COMPLETED);
        EXPECT_EQ(reports[1].status, GYRO_COMPLETED);
    }

    // -----------------------------------------------------------------------
    // Addresses
    //
    // Given addresses are read in the call, asked-for ones are written later.
    // -----------------------------------------------------------------------

    TEST_F(UdpTest, ADestinationMayGoAwayOnceTheSendReturns) {
        // Holding the direction keeps the send out of the fast path, so it is
        // the loop that sends it, long after the address below is gone.
        ASSERT_NE(gyro_handle_try_begin(GYRO_HANDLE(this->b), GYRO_DIR_OUT), 0);

        char message[] = "kept";
        gyro_buf_t out{message, 4};
        Report sent;
        int rc;
        {
            sockaddr_storage dest = this->addr_a;

            rc = gyro_udp_send(this->b, &out, 1, (sockaddr *) &dest, 0, Done, &sent, nullptr, nullptr);

            // Garbage where the address was, family included: a send still
            // reading through the caller's pointer would go nowhere.
            memset(&dest, 0xAB, sizeof(dest));
        }

        gyro_handle_try_end(GYRO_HANDLE(this->b), GYRO_DIR_OUT);

        ASSERT_EQ(rc, GYRO_PENDING);

        char storage[16] = {};
        gyro_buf_t in{storage, sizeof(storage)};
        Report got;
        ASSERT_EQ(gyro_udp_recv(this->a, &in, 1, nullptr, 0, Done, &got, nullptr, nullptr), GYRO_PENDING);

        RunFor(2);

        EXPECT_EQ(sent.status, GYRO_COMPLETED);
        EXPECT_EQ(sent.transferred, 4u);
        EXPECT_EQ(got.status, GYRO_COMPLETED);
        EXPECT_EQ(memcmp(storage, "kept", 4), 0);
    }

    TEST_F(UdpTest, AnAddressInAStorageIsReadForItsFamilyAlone) {
        // A sockaddr_in inside a sockaddr_storage, which is what getsockname()
        // and getaddrinfo() hand out. Darwin refuses the storage's own size as
        // a length, so this only works because gyro passes the family's.
        static_assert(sizeof(sockaddr_storage) > sizeof(sockaddr_in));
        ASSERT_EQ(this->addr_a.ss_family, AF_INET);

        char message[] = "fits";
        gyro_buf_t out{message, 4};

        EXPECT_EQ(gyro_udp_send(this->b, &out, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, nullptr),
                  GYRO_COMPLETED);
    }

    // -----------------------------------------------------------------------
    // Connected sockets
    //
    // Nothing travels on connect: it fixes a peer, filters everybody else out,
    // and lets the network's refusals reach the application.
    // -----------------------------------------------------------------------

    TEST_F(UdpTest, AConnectedSocketSendsWithoutADestination) {
        ASSERT_EQ(gyro_udp_connect(this->b, (sockaddr *) &this->addr_a), GYRO_COMPLETED);

        char message[] = "peer";
        gyro_buf_t out{message, 4};

        ASSERT_EQ(gyro_udp_send(this->b, &out, 1, nullptr, 0, nullptr, nullptr, nullptr, nullptr), GYRO_COMPLETED);

        char storage[16] = {};
        gyro_buf_t in{storage, sizeof(storage)};
        size_t got = 0;

        WaitReadable(this->a);
        EXPECT_EQ(gyro_udp_recv(this->a, &in, 1, nullptr, 0, nullptr, nullptr, nullptr, &got), GYRO_COMPLETED);
        EXPECT_EQ(got, 4u);
        EXPECT_EQ(memcmp(storage, "peer", 4), 0);
    }

    TEST_F(UdpTest, ASendWithNowhereToGoIsNotConnected) {
        char message[] = "lost";
        gyro_buf_t out{message, 4};

        EXPECT_EQ(gyro_udp_send(this->b, &out, 1, nullptr, 0, nullptr, nullptr, nullptr, nullptr), GYRO_ENOTCONN);
    }

    TEST_F(UdpTest, AConnectedSocketHearsOnlyItsPeer) {
        auto *stranger = NewBound();

        ASSERT_EQ(gyro_udp_connect(this->a, (sockaddr *) &this->addr_b), GYRO_COMPLETED);

        char noise[] = "no", signal[] = "yes";
        gyro_buf_t n{noise, 2}, s{signal, 3};

        // The stranger speaks first. Had it been let through, it would be the
        // first datagram in line.
        ASSERT_EQ(gyro_udp_send(stranger, &n, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, nullptr),
                  GYRO_COMPLETED);
        ASSERT_EQ(gyro_udp_send(this->b, &s, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, nullptr),
                  GYRO_COMPLETED);

        char storage[16] = {};
        gyro_buf_t in{storage, sizeof(storage)};
        size_t got = 0;

        WaitReadable(this->a);
        EXPECT_EQ(gyro_udp_recv(this->a, &in, 1, nullptr, 0, nullptr, nullptr, nullptr, &got), GYRO_COMPLETED);
        EXPECT_EQ(got, 3u);
        EXPECT_EQ(memcmp(storage, "yes", 3), 0);

        CloseAndWait(stranger);
    }

    TEST_F(UdpTest, ARefusalFromThePeerReachesAConnectedSocket) {
        // An address nobody is listening on any more.
        auto *gone = NewBound();
        const auto dead = AddressOf(gone);
        CloseAndWait(gone);

        ASSERT_EQ(gyro_udp_connect(this->a, (sockaddr *) &dead), GYRO_COMPLETED);

        char message[] = "x";
        gyro_buf_t out{message, 1};
        ASSERT_EQ(gyro_udp_send(this->a, &out, 1, nullptr, 0, nullptr, nullptr, nullptr, nullptr), GYRO_COMPLETED);

        // The port-unreachable that comes back is parked on the socket, and
        // the receive is what collects it. Whether it has arrived by the time
        // the receive is made decides only which way it reports.
        char storage[8];
        gyro_buf_t in{storage, sizeof(storage)};
        Report report;

        const int rc = gyro_udp_recv(this->a, &in, 1, nullptr, 2000, Done, &report, nullptr, nullptr);
        if (rc == GYRO_PENDING) {
            RunFor(1);
            EXPECT_EQ(report.status, GYRO_ECONNREFUSED);
        } else
            EXPECT_EQ(rc, GYRO_ECONNREFUSED);
    }

    // -----------------------------------------------------------------------
    // Lifecycle
    //
    // The same tokens, deadlines, cancellation and ordered close a TCP handle
    // gets, checked here because the operations underneath are this file's.
    // -----------------------------------------------------------------------

    TEST_F(UdpTest, CancellingAReceiveReportsItAtOnce) {
        char storage[16];
        gyro_buf_t in{storage, sizeof(storage)};
        Report report;
        gyro_request_t token;

        ASSERT_EQ(gyro_udp_recv(this->a, &in, 1, nullptr, 0, Done, &report, &token, nullptr), GYRO_PENDING);
        ASSERT_TRUE(GYRO_REQUEST_IS_VALID(token));

        ASSERT_EQ(gyro_request_cancel(this->loop, token), GYRO_COMPLETED);

        RunFor(1);

        EXPECT_EQ(report.calls, 1);
        EXPECT_EQ(report.status, GYRO_ECANCELED);
    }

    TEST_F(UdpTest, ADeadlineGivesUpOnAReceiveNobodyAnswers) {
        char storage[16];
        gyro_buf_t in{storage, sizeof(storage)};
        Report report;

        ASSERT_EQ(gyro_udp_recv(this->a, &in, 1, nullptr, 30, Done, &report, nullptr, nullptr), GYRO_PENDING);

        RunFor(1);

        EXPECT_EQ(report.calls, 1);
        EXPECT_EQ(report.status, GYRO_ETIMEDOUT);
        EXPECT_EQ(report.transferred, 0u);
    }

    TEST_F(UdpTest, ClosingReportsEveryPendingReceiveFirst) {
        char storage[16];
        gyro_buf_t in{storage, sizeof(storage)};
        sockaddr_storage from{};
        Report report;

        ASSERT_EQ(gyro_udp_recv(this->a, &in, 1, &from, 0, Done, &report, nullptr, nullptr), GYRO_PENDING);

        gyro_handle_close(GYRO_HANDLE(this->a), OnClose);

        // One for the receive and one for the close.
        RunFor(2);
        this->a = nullptr;

        EXPECT_EQ(report.calls, 1);
        EXPECT_EQ(report.status, GYRO_ECANCELED);
        EXPECT_EQ(this->closed, 1);
    }

    TEST_F(UdpTest, SubmittingOnAClosingHandleFails) {
        char storage[16];
        gyro_buf_t buf{storage, sizeof(storage)};

        gyro_handle_close(GYRO_HANDLE(this->a), OnClose);

        EXPECT_EQ(gyro_udp_recv(this->a, &buf, 1, nullptr, 0, nullptr, nullptr, nullptr, nullptr), GYRO_EBADF);
        EXPECT_EQ(gyro_udp_send(this->a, &buf, 1, (sockaddr *) &this->addr_b, 0, nullptr, nullptr, nullptr, nullptr),
                  GYRO_EBADF);

        RunFor(1);
        this->a = nullptr;

        EXPECT_EQ(this->closed, 1);
    }

    // -----------------------------------------------------------------------
    // Threads
    // -----------------------------------------------------------------------

    TEST_F(UdpTest, ASendFromAnotherThreadIsCarriedOutOnThatThread) {
        char message[] = "hello";
        gyro_buf_t out{message, 5};

        int rc = GYRO_EUNKNOWN;
        size_t sent = 0;

        std::thread other([&] {
            rc = gyro_udp_send(this->b, &out, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, &sent);
        });
        other.join();

        // The loop is not running and never sees it.
        EXPECT_EQ(rc, GYRO_COMPLETED);
        EXPECT_EQ(sent, 5u);
        EXPECT_EQ(gyro_handle_pending(GYRO_HANDLE(this->b), GYRO_DIR_OUT), 0u);
    }

    TEST_F(UdpTest, AReceiveFromAnotherThreadIsCarriedOutOnThatThread) {
        char message[] = "hello";
        gyro_buf_t out{message, 5};
        ASSERT_EQ(gyro_udp_send(this->b, &out, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, nullptr),
                  GYRO_COMPLETED);

        WaitReadable(this->a);

        char storage[16] = {};
        gyro_buf_t in{storage, sizeof(storage)};
        sockaddr_storage from{};

        int rc = GYRO_EUNKNOWN;
        size_t got = 0;

        std::thread other([&] {
            rc = gyro_udp_recv(this->a, &in, 1, &from, 0, nullptr, nullptr, nullptr, &got);
        });
        other.join();

        EXPECT_EQ(rc, GYRO_COMPLETED);
        EXPECT_EQ(got, 5u);
        EXPECT_EQ(memcmp(storage, "hello", 5), 0);
        EXPECT_EQ(PortOf(from), PortOf(this->addr_b));
        EXPECT_EQ(gyro_handle_pending(GYRO_HANDLE(this->a), GYRO_DIR_IN), 0u);
    }

    TEST_F(UdpTest, AReceiveQueuedFromAnotherThreadWakesASleepingLoop) {
        // Something to keep the loop busy and asleep: a receive nobody will
        // answer, with a deadline far enough away to tell a wakeup from the
        // loop merely coming round on its own.
        char idle[8];
        gyro_buf_t idlebuf{idle, sizeof(idle)};
        ASSERT_EQ(gyro_udp_recv(this->b, &idlebuf, 1, nullptr, 5000, nullptr, nullptr, nullptr, nullptr),
                  GYRO_PENDING);

        char storage[16] = {};
        gyro_buf_t in{storage, sizeof(storage)};
        Report report;

        char message[] = "woke";
        gyro_buf_t out{message, 4};

        std::thread other([&] {
            std::this_thread::sleep_for(50ms);

            // Nothing is waiting yet, so this goes to the loop rather than
            // inline, and the loop has to wake up to start watching for it.
            EXPECT_EQ(gyro_udp_recv(this->a, &in, 1, nullptr, 0, Done, &report, nullptr, nullptr), GYRO_PENDING);

            std::this_thread::sleep_for(50ms);

            EXPECT_EQ(gyro_udp_send(this->b, &out, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr,
                                    nullptr), GYRO_COMPLETED);
        });

        const auto started = std::chrono::steady_clock::now();
        RunFor(1);
        const auto elapsed = std::chrono::steady_clock::now() - started;

        other.join();

        EXPECT_EQ(report.status, GYRO_COMPLETED);
        EXPECT_EQ(memcmp(storage, "woke", 4), 0);
        EXPECT_LT(elapsed, 2000ms) << "the loop only noticed the receive when the idle deadline woke it";
    }

    // -----------------------------------------------------------------------
    // Overlap
    //
    // Threads sending on one handle while the loop receives, all at the same
    // time, which is what a thread sanitiser needs to have anything to say.
    // Asserts on what holds however the interleaving falls: every datagram
    // whole, none lost over loopback, and the claims back to zero.
    // -----------------------------------------------------------------------

    /// Receives on the loop's thread until it has seen as many as expected.
    struct Receiver {
        gyro_t *loop;
        gyro_udp_t *udp;

        int expected;
        int received = 0;
        int failures = 0;
        size_t bytes = 0;

        char storage[64] = {};
        gyro_buf_t buf{storage, sizeof(storage)};

        void Take(const int status, const size_t n) {
            if (status != GYRO_COMPLETED) {
                this->failures++;
                return;
            }

            this->received++;
            this->bytes += n;
        }

        /// Keeps one receive outstanding, taking inline whatever is already
        /// there, and stops the loop once everything has arrived.
        void Next() {
            while (this->received < this->expected && this->failures == 0) {
                size_t n = 0;

                // A deadline so that a datagram lost after all ends the test
                // with a failure rather than a hang.
                const int rc = gyro_udp_recv(this->udp, &this->buf, 1, nullptr, 2000, OnDatagram, this, nullptr, &n);
                if (rc == GYRO_PENDING)
                    return;

                Take(rc, n);
            }

            gyro_stop(this->loop);
        }

        static gyro_cb_status_t OnDatagram(gyro_handle_t *, const int status, const size_t n, void *data) {
            auto *self = (Receiver *) data;

            self->Take(status, n);
            self->Next();

            return GYRO_CB_SUCCESS;
        }
    };

    TEST_F(UdpTest, SendsFromManyThreadsOverlapWithALoopReceiving) {
        constexpr int kThreads = 4;
        constexpr int kEach = 16;
        constexpr size_t kBytes = 4;
        constexpr int kTotal = kThreads * kEach;

        Receiver receiver{this->loop, this->a, kTotal};
        receiver.Next();

        std::atomic<int> refused{0};
        std::vector<std::thread> senders;

        for (int t = 0; t < kThreads; t++) {
            senders.emplace_back([&] {
                char message[kBytes] = {'d', 'a', 't', 'a'};
                gyro_buf_t out{message, kBytes};

                for (int i = 0; i < kEach; i++) {
                    // Queued when another thread holds the direction, so the
                    // buffer has to outlive the send: it lives as long as the
                    // thread, and the loop has drained the queue by then.
                    const int rc = gyro_udp_send(this->b, &out, 1, (sockaddr *) &this->addr_a, 0, nullptr,
                                                 nullptr, nullptr, nullptr);
                    if (rc != GYRO_COMPLETED && rc != GYRO_PENDING)
                        refused.fetch_add(1);
                }

                // Only leave once nothing of this thread's is still queued.
                while (gyro_handle_pending(GYRO_HANDLE(this->b), GYRO_DIR_OUT) != 0)
                    std::this_thread::sleep_for(1ms);
            });
        }

        UdpTest::current = this;
        EXPECT_EQ(gyro_run(this->loop), GYRO_STOPPED);

        for (auto &sender: senders)
            sender.join();

        EXPECT_EQ(refused.load(), 0);
        EXPECT_EQ(receiver.failures, 0);
        EXPECT_EQ(receiver.received, kTotal);
        EXPECT_EQ(receiver.bytes, kTotal * kBytes) << "a datagram arrived in pieces";

        EXPECT_EQ(gyro_handle_pending(GYRO_HANDLE(this->a), GYRO_DIR_IN), 0u);
        EXPECT_EQ(gyro_handle_pending(GYRO_HANDLE(this->b), GYRO_DIR_OUT), 0u);
    }

    // -----------------------------------------------------------------------
    // Setting up
    // -----------------------------------------------------------------------

    TEST_F(UdpTest, OpeningTheDescriptorEarlyLeavesRoomToConfigureIt) {
        auto *early = gyro_udp_new(this->loop);
        ASSERT_NE(early, nullptr);
        EXPECT_EQ(gyro_udp_fileno(early), GYRO_INVALID_SOCKET);

        ASSERT_EQ(gyro_udp_open(early, AF_INET), GYRO_COMPLETED);

        const auto fd = gyro_udp_fileno(early);
        ASSERT_NE(fd, GYRO_INVALID_SOCKET);

        // The moment gyro has no call for: an option set before binding.
        constexpr int kRoom = 1 << 16;
        EXPECT_EQ(setsockopt((int) fd, SOL_SOCKET, SO_RCVBUF, &kRoom, sizeof(kRoom)), 0);

        // Idempotent, and bind keeps the descriptor rather than opening another.
        EXPECT_EQ(gyro_udp_open(early, AF_INET), GYRO_COMPLETED);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        EXPECT_EQ(gyro_udp_bind(early, (sockaddr *) &addr, 0), GYRO_COMPLETED);
        EXPECT_EQ(gyro_udp_fileno(early), fd);

        CloseAndWait(early);
    }

    TEST_F(UdpTest, AnOpenSocketCanSendWithoutBeingBound) {
        auto *sender = gyro_udp_new(this->loop);
        ASSERT_NE(sender, nullptr);
        ASSERT_EQ(gyro_udp_open(sender, AF_INET), GYRO_COMPLETED);

        char message[] = "anon";
        gyro_buf_t out{message, 4};
        ASSERT_EQ(gyro_udp_send(sender, &out, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, nullptr),
                  GYRO_COMPLETED);

        char storage[16] = {};
        gyro_buf_t in{storage, sizeof(storage)};
        sockaddr_storage from{};
        size_t got = 0;

        // The kernel picked a port on the first send, and that is what the
        // receiver sees.
        WaitReadable(this->a);
        EXPECT_EQ(gyro_udp_recv(this->a, &in, 1, &from, 0, nullptr, nullptr, nullptr, &got), GYRO_COMPLETED);
        EXPECT_EQ(got, 4u);
        EXPECT_EQ(PortOf(from), PortOf(AddressOf(sender)));

        CloseAndWait(sender);
    }

    TEST_F(UdpTest, AnIpv6OnlySocketLeavesThePortToIpv4) {
        auto *v6 = gyro_udp_new(this->loop);
        auto *v4 = gyro_udp_new(this->loop);
        ASSERT_NE(v6, nullptr);
        ASSERT_NE(v4, nullptr);

        sockaddr_in6 any6{};
        any6.sin6_family = AF_INET6;
        any6.sin6_addr = in6addr_any;

        const int rc = gyro_udp_bind(v6, (sockaddr *) &any6, GYRO_BIND_IPV6ONLY);
        if (rc == GYRO_ENOTSUP || rc == GYRO_EADDRNOTAVAIL) {
            CloseAndWait(v6);
            CloseAndWait(v4);
            GTEST_SKIP() << "no IPv6 on this host";
        }
        ASSERT_EQ(rc, GYRO_COMPLETED);

        int v6only = 0;
        socklen_t len = sizeof(v6only);
        ASSERT_EQ(getsockopt((int) gyro_udp_fileno(v6), IPPROTO_IPV6, IPV6_V6ONLY, &v6only, &len), 0);
        EXPECT_EQ(v6only, 1);

        // The same port for IPv4, with no REUSEADDR to help: free only because
        // the IPv6 socket did not take it for both families.
        sockaddr_in6 bound{};
        len = sizeof(bound);
        ASSERT_EQ(getsockname((int) gyro_udp_fileno(v6), (sockaddr *) &bound, &len), 0);

        sockaddr_in any4{};
        any4.sin_family = AF_INET;
        any4.sin_addr.s_addr = htonl(INADDR_ANY);
        any4.sin_port = bound.sin6_port;

        EXPECT_EQ(gyro_udp_bind(v4, (sockaddr *) &any4, 0), GYRO_COMPLETED);

        CloseAndWait(v6);
        CloseAndWait(v4);
    }

    // -----------------------------------------------------------------------
    // Refusals
    //
    // Everything the header says is refused, refused before anything is
    // opened or queued.
    // -----------------------------------------------------------------------

    TEST_F(UdpTest, ArgumentsTheHeaderRefusesAreRefused) {
        char storage[8];
        gyro_buf_t buf{storage, sizeof(storage)};
        const auto *to = (sockaddr *) &this->addr_a;

        // Regions: none, or more than one syscall takes, which would have to
        // become two datagrams.
        EXPECT_EQ(gyro_udp_recv(this->a, nullptr, 1, nullptr, 0, nullptr, nullptr, nullptr, nullptr), GYRO_EINVAL);
        EXPECT_EQ(gyro_udp_recv(this->a, &buf, 0, nullptr, 0, nullptr, nullptr, nullptr, nullptr), GYRO_EINVAL);
        EXPECT_EQ(gyro_udp_recv(this->a, &buf, IOV_MAX + 1, nullptr, 0, nullptr, nullptr, nullptr, nullptr),
                  GYRO_EINVAL);
        EXPECT_EQ(gyro_udp_send(this->b, nullptr, 1, to, 0, nullptr, nullptr, nullptr, nullptr), GYRO_EINVAL);
        EXPECT_EQ(gyro_udp_send(this->b, &buf, 0, to, 0, nullptr, nullptr, nullptr, nullptr), GYRO_EINVAL);
        EXPECT_EQ(gyro_udp_send(this->b, &buf, IOV_MAX + 1, to, 0, nullptr, nullptr, nullptr, nullptr), GYRO_EINVAL);

        // Addresses of any family but the two IP ones.
        sockaddr unspec{};
        unspec.sa_family = AF_UNSPEC;
        EXPECT_EQ(gyro_udp_send(this->b, &buf, 1, &unspec, 0, nullptr, nullptr, nullptr, nullptr), GYRO_EINVAL);
        EXPECT_EQ(gyro_udp_connect(this->b, &unspec), GYRO_EINVAL);
        EXPECT_EQ(gyro_udp_bind(this->b, &unspec, 0), GYRO_EINVAL);
        EXPECT_EQ(gyro_udp_open(this->b, AF_UNIX), GYRO_EINVAL);

        // No handle at all.
        EXPECT_EQ(gyro_udp_new(nullptr), nullptr);
        EXPECT_EQ(gyro_udp_fileno(nullptr), GYRO_INVALID_SOCKET);
        EXPECT_EQ(gyro_udp_open(nullptr, AF_INET), GYRO_EINVAL);
        EXPECT_EQ(gyro_udp_bind(nullptr, to, 0), GYRO_EINVAL);
        EXPECT_EQ(gyro_udp_connect(nullptr, to), GYRO_EINVAL);
        EXPECT_EQ(gyro_udp_recv(nullptr, &buf, 1, nullptr, 0, nullptr, nullptr, nullptr, nullptr), GYRO_EINVAL);
        EXPECT_EQ(gyro_udp_send(nullptr, &buf, 1, to, 0, nullptr, nullptr, nullptr, nullptr), GYRO_EINVAL);

        // Nothing was left outstanding by any of it.
        EXPECT_EQ(gyro_handle_pending(GYRO_HANDLE(this->a), GYRO_DIR_IN), 0u);
        EXPECT_EQ(gyro_handle_pending(GYRO_HANDLE(this->b), GYRO_DIR_OUT), 0u);
    }

    TEST_F(UdpTest, AHandleWithNoDescriptorYetCannotSendOrReceive) {
        auto *fresh = gyro_udp_new(this->loop);
        ASSERT_NE(fresh, nullptr);

        char storage[8];
        gyro_buf_t buf{storage, sizeof(storage)};

        // Its family is not known until bind, connect or open says it.
        EXPECT_EQ(gyro_udp_recv(fresh, &buf, 1, nullptr, 0, nullptr, nullptr, nullptr, nullptr), GYRO_EINVAL);
        EXPECT_EQ(gyro_udp_send(fresh, &buf, 1, (sockaddr *) &this->addr_a, 0, nullptr, nullptr, nullptr, nullptr),
                  GYRO_EINVAL);
        EXPECT_EQ(gyro_udp_fileno(fresh), GYRO_INVALID_SOCKET);

        CloseAndWait(fresh);
    }

    TEST_F(UdpTest, ABindRefusedForItsFlagsOpensNothing) {
        auto *fresh = gyro_udp_new(this->loop);
        ASSERT_NE(fresh, nullptr);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        // A bit gyro does not know, and a known one that cannot apply.
        EXPECT_EQ(gyro_udp_bind(fresh, (sockaddr *) &addr, 0x80), GYRO_EINVAL);
        EXPECT_EQ(gyro_udp_bind(fresh, (sockaddr *) &addr, GYRO_BIND_IPV6ONLY), GYRO_EINVAL);

        EXPECT_EQ(gyro_udp_fileno(fresh), GYRO_INVALID_SOCKET) << "a refused bind left a descriptor behind";

        CloseAndWait(fresh);
    }

    TEST(BindFlagsTest, TheLibraryKnowsTheFlagsItsHeaderNames) {
        EXPECT_TRUE(gyro_bind_flags_supported(0));
        EXPECT_TRUE(gyro_bind_flags_supported(GYRO_BIND_REUSEADDR));
        EXPECT_TRUE(gyro_bind_flags_supported(GYRO_BIND_IPV6ONLY));
        EXPECT_TRUE(gyro_bind_flags_supported(GYRO_BIND_REUSEADDR | GYRO_BIND_IPV6ONLY));

        // One unknown bit is enough to say no, whatever comes with it.
        EXPECT_FALSE(gyro_bind_flags_supported(0x80));
        EXPECT_FALSE(gyro_bind_flags_supported(GYRO_BIND_REUSEADDR | 0x80));
    }
} // namespace
