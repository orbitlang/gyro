// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <gyro/platform.h>

#if GYRO_OS_POSIX

#include <cerrno>
#include <climits> // IOV_MAX
#include <cstring>
#include <new>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <gyro/error.h>
#include <gyro/udp.h>

#include "error_internal.h"
#include "gyro_internal.h"
#include "handle_internal.h"
#include "socket.h"

static_assert(sizeof(sockaddr_in6) <= sizeof(GyroRequest::to),
              "GyroRequest::to must hold a sockaddr_in6");

struct GyroUdp {
    GyroHandle handle;
};

// ---------------------------------------------------------------------------
// Transfers
// ---------------------------------------------------------------------------

static int RecvOnce(const int fd, const gyro_buf_t *bufs, const unsigned int nbufs, sockaddr_storage *from, size_t *n) {
    msghdr msg{};

    msg.msg_name = from;
    msg.msg_namelen = from != nullptr ? (socklen_t) sizeof(sockaddr_storage) : 0;
    msg.msg_iov = (iovec *) bufs;
    msg.msg_iovlen = (int) nbufs;

    ssize_t ret;
    do
        ret = recvmsg(fd, &msg, 0);
    while (ret < 0 && errno == EINTR);

    if (ret < 0) {
        if (n != nullptr)
            *n = 0;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return GYRO_PENDING;

        return gyro::ErrorToStatus(errno);
    }

    if (n != nullptr)
        *n = (size_t) ret;

    return (msg.msg_flags & MSG_TRUNC) ? GYRO_EMSGSIZE : GYRO_COMPLETED;
}

static int SendOnce(const int fd, const gyro_buf_t *bufs, const unsigned int nbufs, const sockaddr *to,
                    const socklen_t tolen, size_t *n) {
    msghdr msg{};

    msg.msg_name = (void *) to;
    msg.msg_namelen = to != nullptr ? tolen : 0;
    msg.msg_iov = (iovec *) bufs;
    msg.msg_iovlen = (int) nbufs;

    int flags = 0;
#if defined(MSG_NOSIGNAL)
    flags |= MSG_NOSIGNAL;
#endif

    ssize_t ret;
    do
        ret = sendmsg(fd, &msg, flags);
    while (ret < 0 && errno == EINTR);

    if (ret < 0) {
        *n = 0;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return GYRO_PENDING;

        return gyro::ErrorToStatus(errno);
    }

    *n = (size_t) ret;

    return GYRO_COMPLETED;
}

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------


static gyro_cb_status_t UDPRecvOp(gyro_handle_t *handle, gyro_op_t *op) {
    size_t recv_bytes;
    const auto err = RecvOnce(handle->handle, op->io.buf, op->io.nbufs, op->from, &recv_bytes);
    if (err == GYRO_COMPLETED || err == GYRO_EMSGSIZE) {
        gyro_op_complete(op, err, recv_bytes);

        return GYRO_CB_SUCCESS;
    }

    if (err == GYRO_PENDING)
        return GYRO_CB_RETRY;

    gyro_op_complete(op, err, 0);

    return GYRO_CB_FAILURE;
}

static gyro_cb_status_t UDPSendOp(gyro_handle_t *handle, gyro_op_t *op) {
    size_t send_bytes;
    const auto err = SendOnce(handle->handle,
                              op->io.buf,
                              op->io.nbufs,
                              op->io.addrlen != 0 ? (const sockaddr *) op->to : nullptr,
                              (socklen_t) op->io.addrlen,
                              &send_bytes);
    if (err == GYRO_PENDING)
        return GYRO_CB_RETRY;

    gyro_op_complete(op, err, send_bytes);

    return err == GYRO_COMPLETED ? GYRO_CB_SUCCESS : GYRO_CB_FAILURE;
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

extern "C" {
int gyro_udp_bind(gyro_udp_t *udp, const sockaddr *addr, const unsigned int flags) {
    if (udp == nullptr || addr == nullptr)
        return GYRO_EINVAL;

    const auto addrlen = gyro::AddrLen(addr);
    if (addrlen == 0)
        return GYRO_EINVAL;

    if (!gyro::IsActive(&udp->handle))
        return GYRO_EBADF;

    const int status = gyro::OpenSocket(&udp->handle, addr->sa_family, SOCK_DGRAM);
    if (status != GYRO_COMPLETED)
        return status;

    if (flags & GYRO_UDP_REUSEADDR) {
        constexpr int on = 1;

        if (setsockopt(udp->handle.handle, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
            return gyro::ErrorToStatus(errno);
    }

    if (bind(udp->handle.handle, addr, addrlen) < 0)
        return gyro::ErrorToStatus(errno);

    return GYRO_COMPLETED;
}

int gyro_udp_connect(gyro_udp_t *udp, const sockaddr *addr) {
    if (udp == nullptr || addr == nullptr)
        return GYRO_EINVAL;

    const auto addrlen = gyro::AddrLen(addr);
    if (addrlen == 0)
        return GYRO_EINVAL;

    if (!gyro::IsActive(&udp->handle))
        return GYRO_EBADF;

    const auto status = gyro::OpenSocket(&udp->handle, addr->sa_family, SOCK_DGRAM);
    if (status != GYRO_COMPLETED)
        return status;

    if (connect(udp->handle.handle, addr, addrlen) != 0)
        return gyro::ErrorToStatus(errno);

    return GYRO_COMPLETED;
}

int gyro_udp_open(gyro_udp_t *udp, const int family) {
    if (udp == nullptr)
        return GYRO_EINVAL;

    if (family != AF_INET && family != AF_INET6)
        return GYRO_EINVAL;

    if (!gyro::IsActive(&udp->handle))
        return GYRO_EBADF;

    return gyro::OpenSocket(&udp->handle, family, SOCK_DGRAM);
}

int gyro_udp_recv(gyro_udp_t *udp, gyro_buf_t *bufs, const unsigned int nbufs, sockaddr_storage *from,
                  const long long timeout,
                  const gyro_rq_user_cb cb, void *data, gyro_request_t *token, size_t *transferred) {
    if (token != nullptr)
        *token = gyro_request_invalid();

    if (transferred != nullptr)
        *transferred = 0;

    if (bufs == nullptr || nbufs == 0 || nbufs > IOV_MAX)
        return GYRO_EINVAL;

    int status = gyro::CheckSubmittable((gyro_handle_t *) udp);
    if (status != GYRO_COMPLETED)
        return status;

    gyro::InlineGate gate((GyroHandle *) udp, GYRO_DIR_IN);
    if (gate.MayTry()) {
        const auto err = RecvOnce(udp->handle.handle, bufs, nbufs, from, transferred);
        if (err != GYRO_PENDING)
            return err;
    }

    GyroRequest *request;
    status = gyro::NewRequest((GyroHandle *) udp, GYRO_DIR_IN, UDPRecvOp, cb, data, &request);
    if (status != GYRO_COMPLETED)
        return status;

    request->from = from;
    request->io.buf = bufs;
    request->io.nbufs = nbufs;

    gate.Transfer();

    status = gyro::Submit(request, token, timeout);
    if (status != GYRO_COMPLETED)
        return status;

    return GYRO_PENDING;
}

int gyro_udp_send(gyro_udp_t *udp, gyro_buf_t *bufs, const unsigned int nbufs, const sockaddr *to,
                  const long long timeout, const gyro_rq_user_cb cb, void *data, gyro_request_t *token,
                  size_t *transferred) {
    if (token != nullptr)
        *token = gyro_request_invalid();

    if (transferred != nullptr)
        *transferred = 0;

    if (bufs == nullptr || nbufs == 0 || nbufs > IOV_MAX)
        return GYRO_EINVAL;

    socklen_t addrlen = 0;
    if (to != nullptr) {
        addrlen = gyro::AddrLen(to);
        if (addrlen == 0)
            return GYRO_EINVAL;
    }

    int status = gyro::CheckSubmittable((gyro_handle_t *) udp);
    if (status != GYRO_COMPLETED)
        return status;

    gyro::InlineGate gate((GyroHandle *) udp, GYRO_DIR_OUT);
    if (gate.MayTry()) {
        size_t send_bytes;
        const auto err = SendOnce(udp->handle.handle,
                                  bufs,
                                  nbufs,
                                  to,
                                  addrlen,
                                  &send_bytes);
        if (err == GYRO_COMPLETED || err == GYRO_EMSGSIZE) {
            if (transferred != nullptr)
                *transferred = send_bytes;

            return err;
        }

        if (err != GYRO_PENDING)
            return err;
    }

    GyroRequest *request;
    status = gyro::NewRequest((GyroHandle *) udp, GYRO_DIR_OUT, UDPSendOp, cb, data, &request);
    if (status != GYRO_COMPLETED)
        return status;

    if (to != nullptr) {
        memcpy(request->to, to, addrlen);
        request->io.addrlen = addrlen;
    }

    request->io.buf = bufs;
    request->io.nbufs = nbufs;

    gate.Transfer();

    status = gyro::Submit(request, token, timeout);
    if (status != GYRO_COMPLETED)
        return status;

    return GYRO_PENDING;
}

gyro_socket_t gyro_udp_fileno(const gyro_udp_t *udp) {
    if (udp == nullptr || udp->handle.handle == gyro::kInvalidSocket)
        return gyro::kInvalidSocket;

    return udp->handle.handle;
}

gyro_udp_t *gyro_udp_new(gyro_t *gyro) {
    if (gyro == nullptr)
        return nullptr;

    const auto *allocator = &gyro->allocator;

    auto *udp = (GyroUdp *) allocator->alloc(sizeof(GyroUdp), allocator->ctx);
    if (udp != nullptr) {
        new(udp) GyroUdp();

        udp->handle.gyro = gyro;
    }

    return udp;
}
} // extern "C"

#endif // GYRO_OS_POSIX
