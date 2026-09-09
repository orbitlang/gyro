// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <gyro/loop.h>

#include "gyro_internal.h"
#include "handle_internal.h"

extern "C" {
gyro_t *gyro_handle_loop(const gyro_handle_t *handle) {
    return handle->gyro;
}

int gyro_handle_may_try(const gyro_handle_t *handle, const gyro_dir_t direction) {
    if (handle == nullptr)
        return 0;

    // Asked first, because every other question below reads state that only
    // the loop's thread may look at.
    if (!gyro_on_loop_thread(handle->gyro))
        return 0;

    if (handle->state != gyro::HandleState::ACTIVE)
        return 0;

    return gyro::QueueFor(handle, direction)->Count() == 0;
}

unsigned int gyro_handle_pending(const gyro_handle_t *handle, const gyro_dir_t direction) {
    return gyro::QueueFor(handle, direction)->Count();
}

void gyro_handle_close(gyro_handle_t *handle, const gyro_close_cb cb) {
    auto *h = handle;

    if (h->state == gyro::HandleState::CLOSING)
        return;

    h->cb_close = cb;
    h->state = gyro::HandleState::CLOSING;

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
} // extern "C"
