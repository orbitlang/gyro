// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <gyro/loop.h>

#include "gyro_internal.h"
#include "handle_internal.h"

extern "C" {
gyro_t *gyro_handle_loop(const gyro_handle_t *handle) {
    return handle->gyro;
}

int gyro_handle_try_begin(gyro_handle_t *handle, const gyro_dir_t direction) {
    return gyro::PendingFor(handle, direction).fetch_add(1, std::memory_order_acquire) == 0;
}

unsigned int gyro_handle_pending(const gyro_handle_t *handle, const gyro_dir_t direction) {
    return gyro::PendingFor(handle, direction).load(std::memory_order_acquire);
}

void gyro_handle_close(gyro_handle_t *handle, const gyro_close_cb cb) {
    assert(gyro_on_loop_thread(handle->gyro));

    auto *h = handle;

    if (!gyro::IsActive(h))
        return;

    h->cb_close = cb;
    h->state.store(gyro::HandleState::CLOSING, std::memory_order_release);

    for (auto *cursor = h->in.GetHead(); cursor != nullptr; cursor = cursor->queue.next)
        gyro::CancelRequest(cursor);

    for (auto *cursor = h->out.GetHead(); cursor != nullptr; cursor = cursor->queue.next)
        gyro::CancelRequest(cursor);

    handle->gyro->AddToClosingQueue(handle);
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
