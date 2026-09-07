// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_PLATFORM_LINUX_BACKEND_H_
#define GYRO_PLATFORM_LINUX_BACKEND_H_

namespace gyro {
    constexpr uint32_t kMaxEvents = 32;

    struct BackendData {
        int wakeup_fd;
    };
}

#endif // !GYRO_PLATFORM_LINUX_BACKEND_H_
