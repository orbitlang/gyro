// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <gyro/error.h>
#include <gyro/loop.h>
#include <gyro/request.h>

extern "C" {
int gyro_request_cancel(gyro_t *gyro, gyro_request_t token) {
    // TODO: impl this
    return GYRO_COMPLETED;
}
} // extern "C"
