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

    gyro::HandleState state = gyro::HandleState::ACTIVE;
};

#endif // !GYRO_HANDLE_INTERNAL_H_
