// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_HANDLE_H_
#define GYRO_HANDLE_H_

#include <gyro/export.h>
#include <gyro/loop.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Which of a handle's two streams an operation belongs to.
 *
 * Each direction has its own queue and is driven independently, so a read and a
 * write on one handle neither wait for nor block each other.
 */
typedef enum {
    GYRO_DIR_IN, /* arriving: reads, and connections being accepted */
    GYRO_DIR_OUT /* leaving: writes, and connections being made */
} gyro_dir_t;

/// Opaque resource registered with a loop.
typedef struct GyroHandle gyro_handle_t;

/// Upcasts a concrete handle to the generic one.
#define GYRO_HANDLE(x) ((gyro_handle_t *) (x))

/**
 * @brief Invoked once a handle has been closed.
 *
 * Every pending operation on the handle has already reported its outcome by
 * the time this runs. The handle is freed as soon as the callback returns and
 * must not be used afterwards.
 */
typedef void (*gyro_close_cb)(gyro_handle_t *handle);

/**
 * @brief Returns the loop the handle belongs to.
 */
GYRO_API gyro_t *gyro_handle_loop(const gyro_handle_t *handle);

/**
 * @brief Closes the handle.
 *
 * Returns immediately: pending operations are cancelled first, and @p cb is
 * invoked on a later iteration once they have all reported. Calling this again
 * on a closing handle does nothing, and the callback still fires exactly once.
 *
 * @param cb Invoked when the handle is gone. May be NULL.
 */
GYRO_API void gyro_close(gyro_handle_t *handle, gyro_close_cb cb);

/**
 * @brief Returns the user data attached to the handle.
 *
 * Distinct from the data passed to a single operation: this one belongs to the
 * resource and is shared by every operation performed on it.
 */
GYRO_API void *gyro_handle_data(const gyro_handle_t *handle);

/**
 * @brief Attaches user data to the handle.
 */
GYRO_API void gyro_handle_set_data(gyro_handle_t *handle, void *data);

#ifdef __cplusplus
}
#endif

#endif // !GYRO_HANDLE_H_
