// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <cerrno>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/uio.h>

#ifdef __linux__
#include <climits> // IOV_MAX
#endif

#include <unistd.h>

#include <gyro/error.h>
#include <gyro/tcp.h>

#include "error_internal.h"
#include "gyro_internal.h"
#include "handle_internal.h"

struct GyroTcp {
    GyroHandle handle;
};

// ---------------------------------------------------------------------------
// Transfers
// ---------------------------------------------------------------------------

/// Charges @p n transferred bytes against the regions, oldest first.
static void AdvanceBufs(gyro_buf_t **bufs, unsigned int *nbufs, size_t *offset, size_t n) {
    while (n > 0 && *nbufs > 0) {
        const auto left = (*bufs)->len - *offset;

        if (n < left) {
            *offset += n;

            return;
        }

        n -= left;

        (*bufs)++;
        (*nbufs)--;

        *offset = 0;
    }
}

/// Drops regions that have nothing left in them, so the array always starts on
/// real work.
static void SkipEmpty(gyro_buf_t **bufs, unsigned int *nbufs, size_t *offset) {
    while (*nbufs > 0 && ((*bufs)->len - *offset) == 0) {
        (*bufs)++;
        (*nbufs)--;

        *offset = 0;
    }
}

// ---------------------------------------------------------------------------
// Sockets
// ---------------------------------------------------------------------------

/**
 * @brief Puts a descriptor into the state the loop expects of it.
 *
 * Non-blocking because the loop never blocks in a syscall, close-on-exec so a
 * fork elsewhere in the process does not leak the connection, and no SIGPIPE:
 * writing to a socket the peer has closed must return an error, not kill the
 * process. Linux does the last one per call with MSG_NOSIGNAL, the BSDs per
 * socket with SO_NOSIGPIPE, so both are applied where they exist.
 */
static bool ConfigureSocket(const int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return false;

    flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
        return false;

#if defined(SO_NOSIGPIPE)
    constexpr int on = 1;

    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif

    return true;
}

static int AcceptOnce(const int handle, GyroHandle *peer) {
    do {
        const int fd = accept(handle, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EINTR)
                continue;

            // The peer gave up between the kernel queuing the connection and us
            // taking it. Nothing failed on our side, so try the one behind it.
            if (errno == ECONNABORTED)
                continue;

            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return GYRO_PENDING;

            return gyro::ErrorToStatus(errno);
        }

        if (!ConfigureSocket(fd)) {
            const int status = gyro::ErrorToStatus(errno);

            close(fd);

            return status;
        }

        peer->handle = fd;

        return GYRO_COMPLETED;
    } while (true);
}

/**
 * @brief Opens the socket a handle has been waiting for.
 *
 * The family is only known once an address turns up, which is why the handle
 * outlives more than one attempt at this.
 */
static int OpenSocket(GyroTcp *tcp, const int family) {
    if (tcp->handle.handle != gyro::kInvalidSocket)
        return GYRO_COMPLETED;

    const int fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0)
        return gyro::ErrorToStatus(errno);

    if (!ConfigureSocket(fd)) {
        const int error = gyro::ErrorToStatus(errno);

        close(fd);

        return error;
    }

    tcp->handle.handle = fd;

    return GYRO_COMPLETED;
}

static ssize_t ReadOnce(const int fd, const gyro_buf_t *bufs, unsigned int nbufs) {
    if (nbufs > (unsigned int) IOV_MAX)
        nbufs = IOV_MAX;

    ssize_t n;
    do
        n = readv(fd, (const iovec *) bufs, (int) nbufs);
    while (n < 0 && errno == EINTR);

    return n;
}

static ssize_t WriteOnce(const int fd, gyro_buf_t *bufs, unsigned int nbufs, const size_t offset) {
    if (nbufs > (unsigned int) IOV_MAX)
        nbufs = IOV_MAX;

    auto b_saved = bufs[0];

    bufs[0].base += offset;
    bufs[0].len -= offset;

    msghdr msg{};

    msg.msg_iov = (iovec *) bufs;
    msg.msg_iovlen = (int) nbufs;

    int flags = 0;
#if defined(MSG_NOSIGNAL)
    flags |= MSG_NOSIGNAL;
#endif

    ssize_t n;
    do
        n = sendmsg(fd, &msg, flags);
    while (n < 0 && errno == EINTR);

    bufs[0] = b_saved;

    return n;
}

static gyro_cb_status_t TcpAcceptOp(gyro_handle_t *handle, gyro_op_t *op) {
    const int status = AcceptOnce(handle->handle, op->io.peer);
    if (status == GYRO_PENDING)
        return GYRO_CB_RETRY;

    gyro_op_complete(op, status, 0);

    return status == GYRO_COMPLETED ? GYRO_CB_SUCCESS : GYRO_CB_FAILURE;
}

static gyro_cb_status_t TcpConnectOp(gyro_handle_t *handle, gyro_op_t *op) {
    // Writability only says the attempt is over, not that it succeeded: the
    // outcome is parked on the socket and this is the only way to read it.
    int error = 0;
    socklen_t len = sizeof(error);

    if (getsockopt(handle->handle, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
        error = errno;

    if (error != 0) {
        gyro_op_complete(op, gyro::ErrorToStatus(error), 0);

        return GYRO_CB_FAILURE;
    }

    gyro_op_complete(op, GYRO_COMPLETED, 0);

    return GYRO_CB_SUCCESS;
}

static gyro_cb_status_t TcpReadOp(gyro_handle_t *handle, gyro_op_t *op) {
    const ssize_t n = ReadOnce(handle->handle, op->io.buf, op->io.nbufs);
    if (n == 0) {
        gyro_op_complete(op, GYRO_EOF, 0);

        return GYRO_CB_SUCCESS;
    }

    if (n > 0) {
        gyro_op_complete(op, GYRO_COMPLETED, (size_t) n);

        return GYRO_CB_SUCCESS;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return GYRO_CB_RETRY;

    gyro_op_complete(op, gyro::ErrorToStatus(errno), 0);

    return GYRO_CB_FAILURE;
}

static gyro_cb_status_t TcpWriteOp(gyro_handle_t *handle, gyro_op_t *op) {
    do {
        SkipEmpty(&op->io.buf, &op->io.nbufs, &op->io.offset);

        if (op->io.nbufs == 0) {
            gyro_op_complete(op, GYRO_COMPLETED, op->io.transferred);

            return GYRO_CB_SUCCESS;
        }

        const ssize_t n = WriteOnce(handle->handle, op->io.buf, op->io.nbufs, op->io.offset);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return GYRO_CB_RETRY;

            gyro_op_complete(op, gyro::ErrorToStatus(errno), op->io.transferred);

            return GYRO_CB_FAILURE;
        }

        op->io.transferred += (size_t) n;

        AdvanceBufs(&op->io.buf, &op->io.nbufs, &op->io.offset, (size_t) n);
    } while (true);
}

// ---------------------------------------------------------------------------
// Submission
// ---------------------------------------------------------------------------

static int CheckSubmittable(const GyroTcp *tcp) {
    if (tcp == nullptr)
        return GYRO_EINVAL;

    if (!gyro::IsActive(&tcp->handle))
        return GYRO_EBADF;

    if (tcp->handle.handle == gyro::kInvalidSocket)
        return GYRO_EINVAL;

    return GYRO_COMPLETED;
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

extern "C" {
int gyro_tcp_accept(gyro_tcp_t *tcp, gyro_tcp_t *client, const long long timeout,
                    const gyro_rq_user_cb cb, void *data, gyro_request_t *out_token) {
    if (out_token != nullptr)
        *out_token = gyro_request_invalid();

    if (client == nullptr || client->handle.handle != gyro::kInvalidSocket)
        return GYRO_EINVAL;

    auto status = CheckSubmittable(tcp);
    if (status != GYRO_COMPLETED)
        return status;

    gyro::InlineGate gate((GyroHandle *) tcp, GYRO_DIR_IN);

    if (gate.MayTry()) {
        status = AcceptOnce(tcp->handle.handle, (GyroHandle *) client);
        if (status != GYRO_PENDING)
            return status;
    }

    GyroRequest *req;

    status = gyro::NewRequest((GyroHandle *) tcp, GYRO_DIR_IN, TcpAcceptOp, cb, data, &req);
    if (status != GYRO_COMPLETED)
        return status;

    req->io.peer = (GyroHandle *) client;

    // From here the request owns the claim, and a Submit that fails has
    // already dropped it through FinishRequest().
    gate.Transfer();

    status = gyro::Submit(req, out_token, timeout);
    if (status != GYRO_COMPLETED)
        return status;

    return GYRO_PENDING;
}

int gyro_tcp_bind(gyro_tcp_t *tcp, const sockaddr *addr, const size_t addrlen, const unsigned int flags) {
    if (tcp == nullptr || addr == nullptr || addrlen == 0)
        return GYRO_EINVAL;

    if (!gyro::IsActive(&tcp->handle))
        return GYRO_EBADF;

    // Nothing here belongs to the loop: the descriptor is the handle's own,
    // and setting one up is ordered against using it by the caller, not by us.
    const int status = OpenSocket(tcp, addr->sa_family);
    if (status != GYRO_COMPLETED)
        return status;

    if (flags & GYRO_TCP_REUSEADDR) {
        constexpr int on = 1;

        if (setsockopt(tcp->handle.handle, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
            return gyro::ErrorToStatus(errno);
    }

    if (bind(tcp->handle.handle, addr, (socklen_t) addrlen) < 0)
        return gyro::ErrorToStatus(errno);

    return GYRO_COMPLETED;
}

int gyro_tcp_connect(gyro_tcp_t *tcp, const sockaddr *addr, const size_t addrlen, const long long timeout,
                     const gyro_rq_user_cb cb, void *data, gyro_request_t *out_token) {
    if (out_token != nullptr)
        *out_token = gyro_request_invalid();

    if (tcp == nullptr || addr == nullptr || addrlen == 0)
        return GYRO_EINVAL;

    if (!gyro::IsActive(&tcp->handle))
        return GYRO_EBADF;

    const auto status = OpenSocket(tcp, addr->sa_family);
    if (status != GYRO_COMPLETED)
        return status;

    // Claimed for the accounting rather than for exclusion: a connect is what
    // makes the handle usable, so nothing else can be outstanding on it yet,
    // and the attempt below is unconditional. What the claim is for is the
    // request underneath, which has to have one to give back.
    gyro::InlineGate gate((GyroHandle *) tcp, GYRO_DIR_OUT);

    do {
        if (connect(tcp->handle.handle, addr, (socklen_t) addrlen) == 0)
            return GYRO_COMPLETED;
    } while (errno == EINTR);

    if (errno != EINPROGRESS && errno != EALREADY)
        return gyro::ErrorToStatus(errno);

    gate.Transfer();

    return gyro_request_submit((GyroHandle *) tcp, data, TcpConnectOp, cb, out_token, timeout, GYRO_DIR_OUT);
}

int gyro_tcp_listen(const gyro_tcp_t *tcp, const int backlog) {
    if (tcp == nullptr)
        return GYRO_EINVAL;

    if (!gyro::IsActive(&tcp->handle))
        return GYRO_EBADF;

    if (tcp->handle.handle == gyro::kInvalidSocket)
        return GYRO_EINVAL;

    if (listen(tcp->handle.handle, backlog) < 0)
        return gyro::ErrorToStatus(errno);

    return GYRO_COMPLETED;
}

GYRO_API int gyro_tcp_read(gyro_tcp_t *tcp, gyro_buf_t *bufs, const unsigned int nbufs, const long long timeout,
                           const gyro_rq_user_cb cb, void *data, gyro_request_t *token, size_t *transferred) {
    if (token != nullptr)
        *token = gyro_request_invalid();

    if (transferred != nullptr)
        *transferred = 0;

    if (bufs == nullptr || nbufs == 0)
        return GYRO_EINVAL;

    int status = CheckSubmittable(tcp);
    if (status != GYRO_COMPLETED)
        return status;

    gyro::InlineGate gate((GyroHandle *) tcp, GYRO_DIR_IN);
    if (gate.MayTry()) {
        const ssize_t n = ReadOnce(tcp->handle.handle, bufs, nbufs);
        if (n == 0)
            return GYRO_EOF;

        if (n > 0) {
            if (transferred != nullptr)
                *transferred = (size_t) n;

            return GYRO_COMPLETED;
        }

        if (errno != EAGAIN && errno != EWOULDBLOCK)
            return gyro::ErrorToStatus(errno);
    }

    GyroRequest *request;
    status = gyro::NewRequest((GyroHandle *) tcp, GYRO_DIR_IN, TcpReadOp, cb, data, &request);
    if (status != GYRO_COMPLETED)
        return status;

    request->io.buf = bufs;
    request->io.nbufs = nbufs;

    // From here the request owns the claim, and a Submit that fails has
    // already dropped it through FinishRequest().
    gate.Transfer();

    status = gyro::Submit(request, token, timeout);
    if (status != GYRO_COMPLETED)
        return status;

    return GYRO_PENDING;
}

GYRO_API int gyro_tcp_write(gyro_tcp_t *tcp, gyro_buf_t *bufs, unsigned int nbufs, long long timeout,
                            gyro_rq_user_cb cb, void *data, gyro_request_t *token, size_t *transferred) {
    if (token != nullptr)
        *token = gyro_request_invalid();

    if (transferred != nullptr)
        *transferred = 0;

    if (bufs == nullptr || nbufs == 0)
        return GYRO_EINVAL;

    int status = CheckSubmittable(tcp);
    if (status != GYRO_COMPLETED)
        return status;

    size_t offset = 0;
    size_t sent = 0;

    gyro::InlineGate gate((GyroHandle *) tcp, GYRO_DIR_OUT);
    if (gate.MayTry()) {
        do {
            SkipEmpty(&bufs, &nbufs, &offset);

            if (nbufs == 0) {
                if (transferred != nullptr)
                    *transferred = sent;

                return GYRO_COMPLETED;
            }

            const auto n = WriteOnce(tcp->handle.handle, bufs, nbufs, offset);
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    return gyro::ErrorToStatus(errno);

                break;
            }

            sent += (size_t) n;

            AdvanceBufs(&bufs, &nbufs, &offset, (size_t) n);
        } while (true);
    }

    GyroRequest *request;
    status = gyro::NewRequest((GyroHandle *) tcp, GYRO_DIR_OUT, TcpWriteOp, cb, data, &request);
    if (status != GYRO_COMPLETED)
        return status;

    request->io.buf = bufs;
    request->io.nbufs = nbufs;
    request->io.offset = offset;
    request->io.transferred = sent;

    // From here the request owns the claim, and a Submit that fails has
    // already dropped it through FinishRequest().
    gate.Transfer();

    status = gyro::Submit(request, token, timeout);
    if (status != GYRO_COMPLETED)
        return status;

    return GYRO_PENDING;
}

gyro_socket_t gyro_tcp_fileno(const gyro_tcp_t *tcp) {
    if (tcp == nullptr || tcp->handle.handle == gyro::kInvalidSocket)
        return gyro::kInvalidSocket;

    return tcp->handle.handle;
}

gyro_tcp_t *gyro_tcp_new(gyro_t *gyro) {
    if (gyro == nullptr)
        return nullptr;

    const auto *allocator = &gyro->allocator;

    auto *tcp = (GyroTcp *) allocator->alloc(sizeof(GyroTcp), allocator->ctx);
    if (tcp != nullptr) {
        new(tcp) GyroTcp();

        tcp->handle.gyro = gyro;
    }

    return tcp;
}
} // extern "C"
