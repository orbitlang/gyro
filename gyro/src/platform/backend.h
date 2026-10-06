// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_PLATFORM_BACKEND_H_
#define GYRO_PLATFORM_BACKEND_H_

#include <gyro/platform.h>

#if GYRO_OS_WINDOWS
#include "platform/win/backend.h"
#elif GYRO_OS_LINUX
#include "platform/linux/backend.h"
#else
#include "platform/bsd/backend.h"
#endif

#endif // !GYRO_PLATFORM_BACKEND_H_
