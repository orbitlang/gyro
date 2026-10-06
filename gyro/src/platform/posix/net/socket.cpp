// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <gyro/platform.h>

#if GYRO_OS_POSIX

#include <cerrno>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gyro/error.h>

#include "error_internal.h"
#include "socket.h"

int gyro::ConfigureSocket(const OSSocket socket, const bool descriptor_ready) {
    if (!descriptor_ready) {
        int flags = fcntl(socket, F_GETFL, 0);
        if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0)
            return ErrorToStatus(errno);

        flags = fcntl(socket, F_GETFD, 0);
        if (flags < 0 || fcntl(socket, F_SETFD, flags | FD_CLOEXEC) < 0)
            return ErrorToStatus(errno);
    }

#if defined(SO_NOSIGPIPE)
    constexpr int on = 1;

    // Writing to a peer that has closed raises SIGPIPE and kills the process
    // unless it is suppressed, and there are two unrelated ways to do it: once
    // on the socket with this, or on every send with MSG_NOSIGNAL. Neither is
    // the other's fallback, and a platform can have both, so each is applied
    // wherever it exists.
    setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif

    return GYRO_COMPLETED;
}

int gyro::OpenSocket(GyroHandle *handle, const int family, const int type) {
    if (handle->handle != kInvalidSocket)
        return GYRO_COMPLETED;

    const int fd = socket(family, type | kSockCreateFlags, 0);
    if (fd < 0)
        return ErrorToStatus(errno);

    const int status = ConfigureSocket(fd, kDescriptorReady);
    if (status != GYRO_COMPLETED) {
        close(fd);

        return status;
    }

    handle->handle = fd;

    return GYRO_COMPLETED;
}

#endif
