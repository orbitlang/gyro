// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_LOOP_H_
#define GYRO_LOOP_H_

#include <gyro/allocator.h>
#include <gyro/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque event loop.
typedef struct Gyro gyro_t;

GYRO_API gyro_t *gyro_new(const gyro_allocator_t *allocator);

GYRO_API void gyro_free(gyro_t *gyro);

#ifdef __cplusplus
}
#endif

#endif // !GYRO_LOOP_H_
