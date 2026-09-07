// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_HANDLE_INTERNAL_H_
#define GYRO_HANDLE_INTERNAL_H_

#include <gyro/handle.h>
#include <gyro/loop.h>

#include "support/queue.h"
#include "platform/ostypes.h"
#include "request_internal.h"

namespace gyro {
    enum class HandleState {
        ACTIVE,
        CLOSING
    };
} // namespace gyro

struct GyroHandle {
    gyro::support::Queue<GyroRequest> in;

    gyro::support::Queue<GyroRequest> out;

    GyroHandle *next = nullptr;

    gyro_t *gyro = nullptr;

    gyro_close_cb cb_close = nullptr;

    void *data = nullptr;

    gyro::OSSocket handle = gyro::kInvalidSocket;

    gyro::HandleState state = gyro::HandleState::ACTIVE;
};

namespace gyro {
    template<typename T>
    std::conditional_t<
        std::is_const_v<std::remove_pointer_t<std::remove_reference_t<T> > >,
        const support::Queue<GyroRequest>,
        support::Queue<GyroRequest>
    >
    *QueueFor(T handle, const gyro_dir_t direction) {
        return direction == GYRO_DIR_OUT ? &handle->out : &handle->in;
    }

    void IOHandleClose(GyroHandle *handle);
} // namespace gyro

#endif // !GYRO_HANDLE_INTERNAL_H_
