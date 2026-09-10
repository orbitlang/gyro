// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_ALLOCATOR_H_
#define GYRO_ALLOCATOR_H_

#include <stddef.h>

#include <gyro/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Where every allocation gyro makes goes through.
 *
 * gyro never calls malloc or new on its own, so an embedder can account for its
 * memory, put it in an arena, or cap it. The hooks must behave like their
 * standard counterparts: alloc returns NULL rather than aborting on failure,
 * and free tolerates NULL.
 *
 * @p ctx is passed back to both untouched, and is also the natural place to put
 * a lock when the allocator is reached from more than one thread.
 */
typedef struct {
    void *(*alloc)(size_t size, void *ctx);

    void (*free)(void *ptr, void *ctx);

    void *ctx;
} gyro_allocator_t;

/**
 * @brief The allocator used when none is given.
 *
 * malloc and free, with no context. Statically allocated, so the pointer is
 * always valid and needs no cleanup.
 *
 * @note Thread-safe: the returned allocator is static.
 */
GYRO_API const gyro_allocator_t *gyro_default_allocator(void);

#ifdef __cplusplus
}
#endif

#endif // !GYRO_ALLOCATOR_H_
