// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <gyro/platform.h>

#if GYRO_OS_POSIX

#include <cerrno>
#include <new>

#include <sys/socket.h>

#include <gyro/error.h>
#include <gyro/udp.h>
#include <netinet6/in6.h>

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
        *n = 0;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return GYRO_PENDING;

        return gyro::ErrorToStatus(errno);
    }

    *n = (size_t) ret;

    return (msg.msg_flags & MSG_TRUNC) ? GYRO_EMSGSIZE : GYRO_COMPLETED;
}

static int SendOnce(const int fd, const gyro_buf_t *bufs, const unsigned int nbufs, const sockaddr *to,
                    const size_t tolen, size_t *n) {
    msghdr msg{};

    msg.msg_name = (void *) to;
    msg.msg_namelen = to != nullptr ? (socklen_t) tolen : 0;
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
// Public
// ---------------------------------------------------------------------------

extern "C" {
int gyro_udp_bind(gyro_udp_t *udp, const sockaddr *addr, const size_t addrlen, const unsigned int flags) {
    if (udp == nullptr || addr == nullptr || addrlen == 0)
        return GYRO_EINVAL;

    if (addr->sa_family != AF_INET && addr->sa_family != AF_INET6)
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

    if (bind(udp->handle.handle, addr, (socklen_t) addrlen) < 0)
        return gyro::ErrorToStatus(errno);

    return GYRO_COMPLETED;
}

int gyro_udp_connect(gyro_udp_t *udp, const sockaddr *addr, const size_t addrlen) {
    if (udp == nullptr || addr == nullptr || addrlen == 0)
        return GYRO_EINVAL;

    if (addr->sa_family != AF_INET && addr->sa_family != AF_INET6)
        return GYRO_EINVAL;

    if (!gyro::IsActive(&udp->handle))
        return GYRO_EBADF;

    const auto status = gyro::OpenSocket(&udp->handle, addr->sa_family, SOCK_DGRAM);
    if (status != GYRO_COMPLETED)
        return status;

    if (connect(udp->handle.handle, addr, (socklen_t) addrlen) != 0)
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

int gyro_udp_recv(gyro_udp_t *udp, gyro_buf_t *bufs, const unsigned int nbufs, sockaddr *from, size_t *fromlen,
                  const long long timeout, const gyro_rq_user_cb cb, void *data, gyro_request_t *token,
                  size_t *transferred) {
    return GYRO_ENOTSUP;
}

int gyro_udp_send(gyro_udp_t *udp, gyro_buf_t *bufs, const unsigned int nbufs, const sockaddr *to, const size_t tolen,
                  const long long timeout, const gyro_rq_user_cb cb, void *data, gyro_request_t *token,
                  size_t *transferred) {
    return GYRO_ENOTSUP;
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
