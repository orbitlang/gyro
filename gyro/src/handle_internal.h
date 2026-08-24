// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_HANDLE_INTERNAL_H_
#define GYRO_HANDLE_INTERNAL_H_

#include <gyro/handle.h>
#include <gyro/loop.h>

enum class HandleState {
    ACTIVE,
    CLOSING
};

struct GyroHandle {
    gyro_close_cb cb_close;

    gyro_t *gyro;

    void *data;

    HandleState state;
};

#endif // !GYRO_HANDLE_INTERNAL_H_
