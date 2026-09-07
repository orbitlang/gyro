// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>

#include <sys/eventfd.h>
#include <sys/epoll.h>

#include "gyro_internal.h"

#include "platform/linux/backend.h"

using namespace gyro;

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
    ev.data.fd = 0;

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
    // TODO: impl this
    return -1;
}

int gyro::IOSubmit(GyroRequest *request) {
    // TODO: impl this
    return -1;
}

void gyro::IOCleanup(const Gyro *loop) {
    close(loop->backend.wakeup_fd);
    close(loop->handler);
}

void gyro::IOWakeup(const Gyro *loop) {
    // TODO: impl this
}

#endif
