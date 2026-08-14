// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_GYRO_H_
#define GYRO_GYRO_H_

#include <gyro/export.h>
#include <gyro/version.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque event loop.
typedef struct Gyro gyro_t;

GYRO_API gyro_t *gyro_new(void);

GYRO_API void gyro_free(gyro_t *gyro);

#ifdef __cplusplus
}
#endif

#endif // !GYRO_GYRO_H_
