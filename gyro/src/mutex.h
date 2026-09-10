// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_MUTEX_H_
#define GYRO_MUTEX_H_

#include <mutex>

namespace gyro {
    using Mutex = std::mutex;

    using Guard = std::lock_guard<Mutex>;
    using UniqueLock = std::unique_lock<Mutex>;
} // namespace gyro

#endif // !GYRO_MUTEX_H_
