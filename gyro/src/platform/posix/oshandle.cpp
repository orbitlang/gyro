// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#if !defined(WIN32) && !defined(_WIN32) && !defined(__WIN32__) && !defined(__NT__)

#include <unistd.h>

#include "gyro_internal.h"

void gyro::IOHandleClose(GyroHandle *handle) {
    if (handle->handle == kInvalidSocket)
        return;

    close(handle->handle);

    handle->handle = kInvalidSocket;
}

#endif
