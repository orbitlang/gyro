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

/**
 * @brief What a callback tells the loop to do next.
 *
 * Says nothing about how the operation itself went: an operation reports its
 * own outcome through gyro_op_complete(), and these values only steer the loop
 * afterwards. Deliberately distinct from gyro_errno_t, whose codes travel in an
 * int and would otherwise be confusable with these.
 */
typedef enum {
    GYRO_CB_CONTINUE, /* keep the operation alive: re-arm it rather than release it */
    GYRO_CB_FAILURE, /* the operation could not be carried out */
    GYRO_CB_RETRY, /* it would block: leave it queued and wait for readiness */
    GYRO_CB_SUCCESS, /* done, move on to whatever is behind it */
} gyro_cb_status_t;

/// Opaque event loop.
typedef struct Gyro gyro_t;

/**
 * @brief Creates a loop.
 *
 * @param allocator Every allocation the loop makes goes through it, and it must
 *                  outlive the loop. NULL for the default one.
 * @return The loop, or NULL if the allocator refused or the backend could not
 *         be opened.
 */
GYRO_API gyro_t *gyro_new(const gyro_allocator_t *allocator);

/**
 * @brief Runs the loop until it is told to stop.
 *
 * Blocks: it waits for the next thing to happen rather than spinning, and
 * returns only once gyro_stop() has been called or the backend has failed. A
 * loop with nothing left to do does not return on its own.
 *
 * The stop flag is cleared on the way in, so a loop that has been stopped can
 * be run again, and by the same token, a gyro_stop() issued before the loop
 * is running has no effect.
 *
 * @return GYRO_COMPLETED after a clean stop, or a negative status if the
 *         backend failed.
 */
GYRO_API int gyro_run(gyro_t *gyro);

/**
 * @brief Releases the loop and everything it owns.
 *
 * The loop must not be running. Handles are the user's to close first: run the
 * loop once more after gyro_close() so their callbacks fire and their
 * descriptors are given back.
 *
 * Does nothing when @p gyro is NULL.
 */
GYRO_API void gyro_free(gyro_t *gyro);

/**
 * @brief Asks a running loop to return from gyro_run().
 *
 * Wakes the loop rather than waiting for it to notice, so it takes effect even
 * while it is blocked with nothing to do. The current iteration is finished
 * first: callbacks already due still run.
 *
 * This is the one function that may be called from another thread.
 */
GYRO_API void gyro_stop(gyro_t *gyro);

#ifdef __cplusplus
}
#endif

#endif // !GYRO_LOOP_H_
