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

typedef struct {
    void *(*alloc)(size_t size, void *ctx);

    void (*free)(void *ptr, void *ctx);

    void *ctx;
} gyro_allocator_t;

/**
 * @brief Retrieves the default memory allocator.
 *
 * This function provides access to the default memory allocator, which is
 * responsible for managing memory allocation and deallocation. The default
 * allocator uses standard library functions (`malloc` and `free`)
 * for allocating and freeing memory. The function returns a pointer to a
 * statically-defined `gyro_allocator_t` structure that describes the default
 * allocator's behavior.
 *
 * @return A pointer to the default `gyro_allocator_t` instance.
 */
GYRO_API const gyro_allocator_t *gyro_default_allocator(void);

#ifdef __cplusplus
}
#endif

#endif // !GYRO_ALLOCATOR_H_
