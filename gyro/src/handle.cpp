// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <gyro/error.h>
#include <gyro/loop.h>

#include "gyro_internal.h"
#include "handle_internal.h"

extern "C" {
gyro_t *gyro_handle_loop(const gyro_handle_t *handle) {
    return handle->gyro;
}

int gyro_handle_close(gyro_handle_t *handle, const gyro_close_cb cb) {
    auto *loop = handle->gyro;

    gyro::RequestIndex tk{};
    auto *req = loop->requests.Acquire(tk);
    if (req == nullptr)
        return GYRO_ENOMEM;

    auto state = gyro::HandleState::ACTIVE;
    if (!handle->state.compare_exchange_strong(state, gyro::HandleState::CLOSING, std::memory_order_release)) {
        loop->requests.Release(req);

        return GYRO_COMPLETED;
    }

    handle->cb_close = cb;

    req->io.peer = handle;
    req->kind = gyro::RequestKind::CLOSE;

    gyro::PostToLoop(loop, req);

    return GYRO_COMPLETED;
}

int gyro_handle_try_begin(gyro_handle_t *handle, const gyro_dir_t direction) {
    return gyro::PendingFor(handle, direction).fetch_add(1, std::memory_order_acquire) == 0;
}

unsigned int gyro_handle_pending(const gyro_handle_t *handle, const gyro_dir_t direction) {
    return gyro::PendingFor(handle, direction).load(std::memory_order_acquire);
}

void *gyro_handle_data(const gyro_handle_t *handle) {
    return handle->data;
}

void gyro_handle_set_data(gyro_handle_t *handle, void *data) {
    handle->data = data;
}

void gyro_handle_try_end(gyro_handle_t *handle, const gyro_dir_t direction) {
    gyro::PendingFor(handle, direction).fetch_sub(1, std::memory_order_release);
}
} // extern "C"
