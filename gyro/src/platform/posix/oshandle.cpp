// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <gyro/platform.h>

#if GYRO_OS_POSIX

#include <unistd.h>

#include "gyro_internal.h"

void gyro::IOHandleClose(GyroHandle *handle) {
    if (handle->handle == kInvalidSocket)
        return;

    close(handle->handle);

    handle->handle = kInvalidSocket;
}

#endif
