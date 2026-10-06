// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <gyro/platform.h>

#if GYRO_OS_WINDOWS
bool gyro::IOCancel(GyroRequest *request) {
    // TODO: CancelIoEx, the completion packet reports the outcome.
    return false;
}
#endif
