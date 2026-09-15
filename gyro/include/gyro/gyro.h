// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_GYRO_H_
#define GYRO_GYRO_H_

/**
 * @file gyro.h
 * @brief Umbrella header: includes the whole public API.
 *
 * Convenience only. Nothing lives here, and a caller that wants to depend on
 * less can include the individual headers instead.
 */

#include <gyro/allocator.h>
#include <gyro/error.h>
#include <gyro/handle.h>
#include <gyro/loop.h>
#include <gyro/os.h>
#include <gyro/request.h>
#include <gyro/timer.h>
#include <gyro/version.h>

// Network
#include <gyro/tcp.h>

#endif // !GYRO_GYRO_H_
