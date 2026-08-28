// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_PLATFORM_BSD_BACKEND_H_
#define GYRO_PLATFORM_BSD_BACKEND_H_

#include <sys/event.h>

namespace gyro {
    constexpr uint32_t kMaxEvents = 32;

    struct BackendData {
        struct kevent changes[kMaxEvents];

        int nchanges;
    };
}

#endif // !GYRO_PLATFORM_BSD_BACKEND_H_
