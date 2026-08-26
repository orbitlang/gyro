// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#if defined(__APPLE__) || defined(BSD)
#include <cerrno>
#include <ctime>

#include <sys/event.h>
#include <sys/fcntl.h>
#include <unistd.h>

#include <gyro_internal.h>

using namespace gyro;

constexpr uint32_t kMaxEvents = 50;
constexpr uintptr_t kWakeupIdent = 1;

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

    loop->handler = (uintptr_t) fd;

    return true;
}

void gyro::IOCleanup(const Gyro *loop) {
    close((int) loop->handler);
}

void gyro::IOPoll(const Gyro *loop, const long long timeout) {
    struct kevent events[kMaxEvents];
    struct kevent kev[2];

    timespec ts{};
    const timespec *tsp = nullptr;

    if (timeout >= 0) {
        ts.tv_sec = (long) timeout / 1000;
        ts.tv_nsec = (long) ((timeout % 1000) * 1000000);

        tsp = &ts;
    }

    const auto ret = kevent((int) loop->handler, nullptr, 0, events, kMaxEvents, tsp);
    if (ret < 0) {
        if (errno == EINTR)
            return; // Try again

        // Never get here!
        assert(false);
    }

    for (int i = 0; i < ret; i++) {
        if (events[i].filter == EVFILT_USER)
            continue;

        // TODO: impl this
    }
}
#endif
