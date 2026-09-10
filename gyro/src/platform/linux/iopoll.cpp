// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#if defined(__linux__)
#include <cassert>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <unistd.h>

#include <sys/eventfd.h>
#include <sys/epoll.h>

#include <gyro/error.h>

#include "error_internal.h"
#include "gyro_internal.h"
#include "handle_internal.h"

#include "platform/linux/backend.h"

using namespace gyro;

/// Arms the handle for whatever its queues are still waiting on.
static int Arm(const Gyro *loop, GyroHandle *handle) {
    if (!IsActive(handle))
        return GYRO_COMPLETED;

    const uint32_t want = (handle->in.Count() ? EPOLLIN : 0) | (handle->out.Count() ? EPOLLOUT : 0);

    if (want == 0)
        return GYRO_COMPLETED;

    epoll_event ev{};

    ev.events = want | EPOLLONESHOT;
    ev.data.ptr = handle;

    if (epoll_ctl(loop->handler, EPOLL_CTL_MOD, handle->handle, &ev) == 0)
        return GYRO_COMPLETED;

    // Not in the set yet: the first arming is the only one that has to add it.
    if (errno != ENOENT)
        return ErrorToStatus(errno);

    if (epoll_ctl(loop->handler, EPOLL_CTL_ADD, handle->handle, &ev) < 0)
        return ErrorToStatus(errno);

    return GYRO_COMPLETED;
}

bool gyro::IOCancel(GyroRequest *request) {
    (void) request;
    return true;
}

bool gyro::IOInit(Gyro *loop) {
    epoll_event ev{};

    auto epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0)
        return false;

    auto evfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evfd < 0) {
        close(epfd);

        return false;
    }

    ev.events = EPOLLIN;
    ev.data.ptr = nullptr;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, evfd, &ev) < 0) {
        close(evfd);
        close(epfd);

        return false;
    }

    loop->handler = epfd;
    loop->backend.wakeup_fd = evfd;

    return true;
}

int gyro::IOPoll(Gyro *loop, const long long timeout) {
    epoll_event events[kMaxEvents];

    int ms = -1;
    if (timeout >= 0)
        ms = timeout > INT_MAX ? INT_MAX : (int) timeout; // INT_MAX = ~24 days

    const auto ret = epoll_wait(loop->handler, events, kMaxEvents, ms);
    if (ret < 0) {
        if (errno == EINTR)
            return GYRO_COMPLETED;

        return ErrorToStatus(errno);
    }

    for (auto i = 0; i < ret; i++) {
        if (events[i].data.ptr == nullptr) {
            uint64_t one;

            ssize_t status;
            do
                status = read(loop->backend.wakeup_fd, &one, sizeof(one));
            while (status < 0 && errno == EINTR);

            continue;
        }

        auto mask = events[i].events;
        auto *handle = (GyroHandle *) events[i].data.ptr;

        // EPOLLERR and EPOLLHUP arrive unrequested and name no direction. Treat them
        // as readiness on both, and let the syscall say what actually went wrong.
        if (mask & (EPOLLERR | EPOLLHUP))
            mask |= EPOLLIN | EPOLLOUT;

        if (mask & EPOLLIN)
            ProcessHandle(handle, GYRO_DIR_IN);

        if (mask & EPOLLOUT)
            ProcessHandle(handle, GYRO_DIR_OUT);

        const auto status = Arm(loop, handle);
        if (status != GYRO_COMPLETED)
            return status;
    }

    return GYRO_COMPLETED;
}

int gyro::IOSubmit(GyroRequest *request) {
    return Arm(request->loop, request->handle);
}

void gyro::IOCleanup(const Gyro *loop) {
    close(loop->backend.wakeup_fd);
    close(loop->handler);
}

void gyro::IOWakeup(const Gyro *loop) {
    constexpr uint64_t one = 1;

    ssize_t n;
    do
        n = write(loop->backend.wakeup_fd, &one, sizeof(one));
    while (n < 0 && errno == EINTR);

    if (n < 0)
        assert(errno == EAGAIN || errno == EWOULDBLOCK);
}

#endif
