// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_PLATFORM_BACKEND_H_
#define GYRO_PLATFORM_BACKEND_H_

// platform/backend.h
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include "platform/win/backend.h"
#elif defined(__linux__)
#include "platform/linux/backend.h"
#else
#include "platform/bsd/backend.h"
#endif

#endif // !GYRO_PLATFORM_BACKEND_H_
