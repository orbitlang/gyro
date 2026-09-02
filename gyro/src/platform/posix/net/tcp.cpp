// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <cerrno>

#include <fcntl.h>
#include <sys/socket.h>
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

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

extern "C" {
int gyro_tcp_bind(gyro_tcp_t *tcp, const sockaddr *addr, const size_t addrlen, const unsigned int flags) {
    if (tcp == nullptr || addr == nullptr || addrlen == 0)
        return GYRO_EINVAL;

    if (tcp->handle.state != gyro::HandleState::ACTIVE)
        return GYRO_EBADF;

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

int gyro_tcp_listen(const gyro_tcp_t *tcp, const int backlog) {
    if (tcp == nullptr)
        return GYRO_EINVAL;

    if (tcp->handle.state != gyro::HandleState::ACTIVE)
        return GYRO_EBADF;

    if (tcp->handle.handle == gyro::kInvalidSocket)
        return GYRO_EINVAL;

    if (listen(tcp->handle.handle, backlog) < 0)
        return gyro::ErrorToStatus(errno);

    return GYRO_COMPLETED;
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
