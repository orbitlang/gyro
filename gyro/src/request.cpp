// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <gyro/error.h>
#include <gyro/loop.h>
#include <gyro/request.h>

#include "gyro_internal.h"

using namespace gyro;

extern "C" {
int gyro_request_cancel(gyro_t *gyro, gyro_request_t token) {
    // TODO: impl this
    return GYRO_COMPLETED;
}

void gyro_op_complete(gyro_op_t *op, const int status, const size_t transferred) {
    if (op->handle != nullptr) {
        auto *queue = op->direction == HandleDirection::OUT ? &op->handle->out : &op->handle->in;

        queue->Remove(op);
    }

    if (op->cb_user != nullptr)
        op->cb_user(op->handle, status, transferred, op->data);

    FinishRequest(op->loop, op);
}
} // extern "C"
