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
 *
 * @note Thread-safe: the loop a handle belongs to is fixed when it is created.
 */
GYRO_API gyro_t *gyro_handle_loop(const gyro_handle_t *handle);

/**
 * @brief Closes the handle.
 *
 * Returns immediately. The handle stops accepting operations in this call;
 * the ones already in flight are cancelled on the loop's next turn and each
 * reports GYRO_ECANCELED, and @p cb is invoked once they all have. Calling
 * this again on a closing handle does nothing, from any thread, and the
 * callback still fires exactly once: the one registered by the call that got
 * there first.
 *
 * An operation submitted from another thread before this call, and not yet
 * picked up by the loop, is cancelled like the rest: the close travels the
 * same queue and reaches the loop behind it.
 *
 * @param cb Invoked when the handle is gone. May be NULL.
 * @return GYRO_COMPLETED, or GYRO_ENOMEM when the request store could not
 *       provide the note that carries the close to the loop.
 *
 * @warning Closing a handle another thread is submitting on at the same
 * moment is undefined. Whoever closes has to know that nobody else is still
 * using it.
 *
 * @note Thread-safe. From the loop's own thread it takes effect on the next
 *       turn, exactly as it does from anywhere else.
 */
GYRO_API int gyro_handle_close(gyro_handle_t *handle, gyro_close_cb cb);

/**
 * @brief Claims a direction, and says whether the operation may go inline.
 *
 * Always takes the claim, whichever way the answer goes: a caller told no is
 * about to queue an operation, and an operation waiting to be served is
 * exactly what the claim counts. Only one caller can be told yes, so only one
 * can be inside the syscall.
 *
 * What is taken here has to be given back exactly once, and there are only
 * two ways to do it. Either the operation finishes here and now and the caller
 * calls gyro_handle_try_end(), or the operation is submitted and the request
 * carries the claim until it reports. Every path that leaves in between,
 * including one that could not allocate a request or was refused by the
 * submit, has to end it: a claim that is never given back leaves the direction
 * looking busy for good, and that handle silently loses its fast path for the
 * rest of its life.
 *
 * @param direction Which of the handle's two streams the operation belongs to.
 * @return Non-zero when the caller owns the direction and may attempt the
 *       syscall itself. Zero means the claim was still taken, and the
 *       operation has to be queued rather than attempted.
 *
 * @note Thread-safe.
 */
GYRO_API int gyro_handle_try_begin(gyro_handle_t *handle, gyro_dir_t direction);

/**
 * @brief Returns how many operations are outstanding in one direction.
 *
 * Everything submitted and not yet reported, including what another thread
 * has handed to the loop and the loop has not picked up yet. For deciding
 * whether to apply backpressure, and not for deciding whether an operation
 * may go ahead here and now: a zero read here says nothing about the moment
 * after, and gyro_handle_try_begin() is what settles that question by
 * claiming the direction rather than asking about it.
 *
 * @param handle Handle to inspect.
 * @param direction Which of its two streams to count.
 * @return The number outstanding, zero when the direction is idle.
 *
 * @note Thread-safe. A count read from another thread was true when it was
 *       read and says nothing about the moment after.
 */
GYRO_API unsigned int gyro_handle_pending(const gyro_handle_t *handle, gyro_dir_t direction);

/**
 * @brief Returns the user data attached to the handle.
 *
 * Distinct from the data passed to a single operation: this one belongs to the
 * resource and is shared by every operation performed on it.
 *
 * @note Thread-safe as far as gyro is concerned: it never reads this field, so
 *       what guards it is the caller's business.
 */
GYRO_API void *gyro_handle_data(const gyro_handle_t *handle);

/**
 * @brief Attaches user data to the handle.
 *
 * @note Thread-safe as far as gyro is concerned: it never reads this field, so
 *       what guards it is the caller's business.
 */
GYRO_API void gyro_handle_set_data(gyro_handle_t *handle, void *data);

/**
 * @brief Gives back a claim taken by gyro_handle_try_begin().
 *
 * For an operation that ended on the calling thread and left nothing behind.
 * An operation that was submitted must not call this: its request holds the
 * claim, and releases it when it reports.
 *
 * @note Thread-safe.
 */
GYRO_API void gyro_handle_try_end(gyro_handle_t *handle, gyro_dir_t direction);

#ifdef __cplusplus
}
#endif

#endif // !GYRO_HANDLE_H_
