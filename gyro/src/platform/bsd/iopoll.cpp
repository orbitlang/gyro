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

static int AppendChange(Gyro *loop, GyroHandle *handle, const gyro_dir_t direction) {
    int filter = EVFILT_READ;
    if (direction == GYRO_DIR_OUT)
        filter = EVFILT_WRITE;

    if (loop->backend.nchanges == kMaxEvents) {
        const auto error = FlushChanges(loop);
        if (error != GYRO_COMPLETED)
            return error;
    }

    EV_SET(&loop->backend.changes[loop->backend.nchanges++], handle->handle, filter, EV_ADD | EV_ONESHOT, 0, 0, handle);

    return GYRO_COMPLETED;
}

static void ReportFailToQueue(const GyroHandle *handle, const gyro_dir_t direction, const int status) {
    const auto *queue = QueueFor(handle, direction);

    // gyro_op_complete() unlinks each request, so the head advances by itself.
    while (auto *request = queue->GetHead())
        gyro_op_complete(request, status, request->io.transferred);
}

bool gyro::IOCancel(GyroRequest *request) {
    (void)request;
    return true;
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

        const auto direction = events[i].filter == EVFILT_WRITE ? GYRO_DIR_OUT : GYRO_DIR_IN;

        if (events[i].flags & EV_ERROR) {
            const int status = ErrorToStatus((int) events[i].data);

            if (handle->state != HandleState::CLOSING) {
                if (events[i].data == EBADF) {
                    ReportFailToQueue(handle, GYRO_DIR_IN, status);
                    ReportFailToQueue(handle, GYRO_DIR_OUT, status);

                    continue;
                }

                ReportFailToQueue(handle, direction, status);
            }

            continue;
        }

        if (direction == GYRO_DIR_OUT && (events[i].flags & EV_EOF)) {
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

int gyro::IOSubmit(GyroRequest *request) {
    return AppendChange(request->loop, request->handle, request->direction);
}

void gyro::IOCleanup(const Gyro *loop) {
    close(loop->handler);
}

void gyro::IOWakeup(const Gyro *loop) {
    struct kevent kev{};

    EV_SET(&kev, kWakeupIdent, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);

    while (kevent(loop->handler, &kev, 1, nullptr, 0, nullptr) < 0) {
        if (errno == EINTR)
            continue;

        break;
    }
}

#endif
