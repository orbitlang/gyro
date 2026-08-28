// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_HANDLE_INTERNAL_H_
#define GYRO_HANDLE_INTERNAL_H_

#include <gyro/handle.h>
#include <gyro/loop.h>

#include "support/queue.h"
#include "request_internal.h"

namespace gyro {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#define GYRO_OSHANDLE_EMPTY (nullptr)
    using OSHandle = void *;
#else
#define GYRO_OSHANDLE_EMPTY (-1)
    using OSHandle = int;
#endif

    enum class HandleDirection {
        IN,
        OUT
    };

    enum class HandleState {
        ACTIVE,
        CLOSING
    };
} // namespace gyro

struct GyroHandle {
    gyro::support::Queue<GyroRequest> in;

    gyro::support::Queue<GyroRequest> out;

    gyro_t *gyro = nullptr;

    gyro_close_cb cb_close = nullptr;

    void *data = nullptr;

    gyro::OSHandle handle = GYRO_OSHANDLE_EMPTY;

    gyro::HandleState state = gyro::HandleState::ACTIVE;
};

#endif // !GYRO_HANDLE_INTERNAL_H_
