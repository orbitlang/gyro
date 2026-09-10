// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_LOOP_H_
#define GYRO_LOOP_H_

#include <gyro/allocator.h>
#include <gyro/export.h>

/**
 * @file loop.h
 *
 * **Loop affinity.** Nearly everything gyro owns is touched by one thread and no
 * other: the handle queues, the timer heap, the request store. Every entry point
 * in the public API therefore says which side of that line it falls on:
 *
 * - *Loop-affine* means the loop's own thread: the one that called gyro_run(),
 *   or the one that created the loop while it has never run.
 *   gyro_on_loop_thread() answers the question, and debug builds assert on it.
 * - *Thread-safe* means any thread at all.
 *
 * Submitting is the deliberate exception: an operation offered from elsewhere
 * is handed to the loop and carried out there, so a worker can start work
 * without knowing who owns what.
 */

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
 *                outlive the loop. NULL for the default one.
 * @return The loop, or NULL if the allocator refused or the backend could not
 *       be opened.
 *
 * @note Thread-safe: nothing exists to share yet. The thread that calls this
 *       is the loop's own until gyro_run() is called somewhere else.
 */
GYRO_API gyro_t *gyro_new(const gyro_allocator_t *allocator);

/**
 * @brief Releases the loop, once there is nothing left in it.
 *
 * Winding the loop down is the caller's job, and it is the same three steps
 * every time: close the handles with gyro_handle_close(), run the loop so that
 * their operations report and their descriptors go back to the OS, and only
 * then release it. Doing it here instead would mean running user callbacks
 * from inside a teardown the user has already committed to, and waiting on a
 * peer that may never answer.
 *
 * @warning The loop must not be running. Calling this from inside a callback
 * frees the loop out from under the iteration that is still using it, and no
 * return value can rescue the caller from that.
 *
 * @note What it can see is what it still holds: operations that have yet to
 * report, and handles closed but not yet buried. A handle that was never
 * closed at all is unknown to the loop and is not detected, the descriptor
 * simply leaks, exactly as it would have before.
 *
 * @return GYRO_COMPLETED once the loop is gone, or GYRO_EBUSY if it still had
 *       work in it, in which case **nothing was released** and the loop is
 *       still the caller's to wind down and free.
 *
 * @note Loop-affine.
 */
GYRO_API int gyro_free(gyro_t *gyro);

/**
 * @brief Runs the loop until there is nothing left to do, or it is told to stop.
 *
 * Blocks: it waits for the next thing to happen rather than spinning. It comes
 * back on its own once no operation is outstanding and no handle is waiting to
 * be closed, because from there no event could ever arrive, so a loop given
 * work returns when that work is over, and one given none returns at once.
 *
 * The stop flag is cleared on the way in, so a loop that has been stopped can
 * be run again, and by the same token, a gyro_stop() issued before the loop
 * is running has no effect.
 *
 * @return GYRO_COMPLETED when the work ran out, GYRO_STOPPED when gyro_stop()
 *       asked for it, or a negative status if the backend failed. The first
 *       two are told apart because they mean different things to whoever
 *       embeds the loop: one is the end of the job, the other a decision.
 *
 * @note Loop-affine, and the call that decides it: the thread running the loop
 *       is the loop's thread from here on.
 */
GYRO_API int gyro_run(gyro_t *gyro);

/**
 * @brief Tells whether the caller is the thread the loop runs on.
 *
 * The loop handles most of its operations internally. Its queues, timers, and
 * request store are accessed exclusively by a single thread. Submission is the
 * sole exception, as it may occur from any thread. When a submission originates
 * from another thread, it is transferred to the loop thread for execution, with
 * results provided subsequently.
 *
 * This is what code performing its own work on a handle has to ask before
 * touching anything of the loop's. Submitting an operation does not need it:
 * a submit finds out for itself, and works either way.
 *
 * Until the loop has been run for the first time the answer is the thread that
 * created it, so a program that sets everything up before starting the loop is
 * on the loop's thread throughout.
 *
 * Safe to call from any thread: answering this is what it is for.
 *
 * @return Non-zero when the caller is the loop's thread.
 */
GYRO_API int gyro_on_loop_thread(const gyro_t *gyro);

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
