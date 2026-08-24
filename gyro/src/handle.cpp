// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <gyro/loop.h>

#include "handle_internal.h"

extern "C" {
gyro_t *gyro_handle_loop(const gyro_handle_t *handle) {
    return handle->gyro;
}

void gyro_close(gyro_handle_t *handle, const gyro_close_cb cb) {
    auto *h = handle;

    if (h->state == HandleState::CLOSING)
        return;

    h->cb_close = cb;
    h->state = HandleState::CLOSING;
}

void *gyro_handle_data(const gyro_handle_t *handle) {
    return handle->data;
}

void gyro_handle_set_data(gyro_handle_t *handle, void *data) {
    handle->data = data;
}
} // extern "C"
