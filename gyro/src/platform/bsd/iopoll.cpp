// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#if defined(__APPLE__) || defined(BSD)
#include <cerrno>
#include <ctime>

#include <sys/event.h>
#include <sys/fcntl.h>
#include <unistd.h>

#include <gyro/error.h>

#include "backend.h"
#include "error_internal.h"
#include "gyro_internal.h"

using namespace gyro;

constexpr uintptr_t kWakeupIdent = 1;

static int FlushChanges(Gyro *loop) {
    if (loop->backend.nchanges == 0)
        return GYRO_COMPLETED;

    constexpr timespec zero{};
    if (kevent(loop->handler, loop->backend.changes, loop->backend.nchanges, nullptr, 0, &zero) < 0) {
        if (errno == EINTR)
            return GYRO_COMPLETED;

        return ErrorToStatus(errno);
    }

    loop->backend.nchanges = 0;

    return GYRO_COMPLETED;
}

static int AppendChange(Gyro *loop, GyroHandle *handle, const HandleDirection direction) {
    int filter = EVFILT_READ;
    if (direction == HandleDirection::OUT)
        filter = EVFILT_WRITE;

    if (loop->backend.nchanges == kMaxEvents) {
        const auto error = FlushChanges(loop);
        if (error != GYRO_COMPLETED)
            return error;
    }

    EV_SET(&loop->backend.changes[loop->backend.nchanges++], handle->handle, filter, EV_ADD | EV_ONESHOT, 0, 0, handle);

    return GYRO_COMPLETED;
}

static void ReportFailToQueue(const GyroHandle *handle, const HandleDirection direction, const int status) {
    const auto *queue = direction == HandleDirection::OUT ? &handle->out : &handle->in;

    // gyro_op_complete() unlinks each request, so the head advances by itself.
    while (auto *request = queue->GetHead())
        gyro_op_complete(request, status, request->io.transferred);
}

bool gyro::IOInit(Gyro *loop) {
    struct kevent kev{};

    int fd;
    if ((fd = kqueue()) < 0)
        return false;

    EV_SET(&kev, kWakeupIdent, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);

    if (kevent(fd, &kev, 1, nullptr, 0, nullptr) < 0) {
        close(fd);

        return false;
    }

    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        close(fd);

        return false;
    }

    loop->handler = fd;
    loop->backend.nchanges = 0;

    return true;
}

int gyro::IOPoll(Gyro *loop, const long long timeout) {
    struct kevent events[kMaxEvents];

    timespec ts{};
    const timespec *tsp = nullptr;

    if (timeout >= 0) {
        ts.tv_sec = (long) timeout / 1000;
        ts.tv_nsec = (long) ((timeout % 1000) * 1000000);

        tsp = &ts;
    }

    const auto ret = kevent(loop->handler,
                            loop->backend.changes,
                            loop->backend.nchanges,
                            events,
                            kMaxEvents,
                            tsp);
    if (ret < 0) {
        if (errno == EINTR)
            return GYRO_COMPLETED;

        return ErrorToStatus(errno);
    }

    loop->backend.nchanges = 0;

    for (int i = 0; i < ret; i++) {
        if (events[i].filter == EVFILT_USER)
            continue;

        auto *handle = (GyroHandle *) events[i].udata;

        const auto direction = events[i].filter == EVFILT_WRITE ? HandleDirection::OUT : HandleDirection::IN;

        if (events[i].flags & EV_ERROR) {
            const int status = ErrorToStatus((int) events[i].data);

            if (handle->state != HandleState::CLOSING) {
                if (events[i].data == EBADF) {
                    ReportFailToQueue(handle, HandleDirection::IN, status);
                    ReportFailToQueue(handle, HandleDirection::OUT, status);

                    continue;
                }

                ReportFailToQueue(handle, direction, status);
            }

            continue;
        }

        if (direction == HandleDirection::OUT && (events[i].flags & EV_EOF)) {
            const int status = events[i].fflags != 0 ? ErrorToStatus((int) events[i].fflags) : GYRO_EPIPE;

            ReportFailToQueue(handle, direction, status);

            continue;
        }

        if (ProcessHandle(handle, direction)) {
            const auto error = AppendChange(loop, handle, direction);
            if (error != GYRO_COMPLETED)
                return error;
        }
    }

    return GYRO_COMPLETED;
}

void gyro::IOCleanup(const Gyro *loop) {
    close(loop->handler);
}

#endif
