// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_TIMER_H_
#define GYRO_TIMER_H_

#include <gyro/export.h>
#include <gyro/loop.h>
#include <gyro/request.h>

/**
 * @file timer.h
 *
 * A timer is an operation with nothing underneath it: no handle, no
 * descriptor, only a deadline in the loop's heap. Everything else about it is
 * the same as any other operation, on purpose. It reports through the same
 * callback shape, it is named by the same kind of token, and it is stopped
 * with gyro_request_cancel() like everything else.
 *
 * A pending timer keeps the loop alive, exactly as a pending read does:
 * gyro_run() does not return while one is outstanding. A timer that repeats
 * for ever therefore keeps the loop running for ever, and has to be cancelled
 * for the loop to wind down.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Arms a timer.
 *
 * The callback runs on the loop's thread once @p timeout milliseconds have
 * passed, and never inside this call: a timer of 0 fires on the loop's next
 * turn, not now.
 *
 * Time is the loop's own clock, read once per turn. A timer fires on the
 * first turn whose clock is at or past its deadline, so it is never early,
 * and it is late by at most the work the turn in front of it did. Armed from
 * another thread, the interval starts when the loop takes the timer in, which
 * follows the call as closely as the wakeup allows.
 *
 * **Repeating.** Return GYRO_CB_CONTINUE from the callback and the timer is
 * armed again for the same interval, counted from the loop's clock at the
 * moment it fired, and keeps its token. Return GYRO_CB_SUCCESS and it is
 * done. Re-arming from the callback rather than resubmitting is what keeps
 * one token valid for the timer's whole life, and it is also what lets a
 * periodic timer stop itself for free: returning GYRO_CB_SUCCESS ends it
 * with no further callback, where cancelling it from inside its own callback
 * would be acted on after the re-arm and report GYRO_ECANCELED once more.
 * For a first delay that differs from the period, see
 * gyro_timer_start_periodic().
 *
 * @param timeout Milliseconds from now. 0 means the next turn, which is how
 *              work is handed to the loop's thread without waiting for
 *              anything. Note the difference from the I/O operations, where
 *              a timeout of 0 means no deadline at all: a timer with no
 *              deadline would be no timer.
 * @param cb Reports the firing. Its `handle` is NULL and its `transferred`
 *         is 0; its `status` is GYRO_COMPLETED when the timer fired and
 *         GYRO_ECANCELED when it was cancelled. Its return value is only
 *         consulted after a firing: a cancelled timer is never re-armed,
 *         whatever the callback says.
 * @param data Passed back to @p cb untouched.
 * @param out_token Receives the token naming the timer, or NULL when the
 *                caller will never cancel it. A repeating timer keeps the same
 *                token across firings.
 * @return GYRO_PENDING once the loop owns the timer, or a negative status:
 *       GYRO_EINVAL for a negative @p timeout or a NULL @p cb, GYRO_ENOMEM
 *       when the request store could not provide a slot. Never
 *       GYRO_COMPLETED, since a timer never ends inside the call.
 *
 * @note Thread-safe: a timer armed from elsewhere is handed to the loop and
 *       fires there.
 */
GYRO_API int gyro_timer_start(gyro_t *gyro, long long timeout, gyro_rq_user_cb cb, void *data,
                              gyro_request_t *out_token);

/**
 * @brief Arms a timer whose first firing and later ones are spaced apart.
 *
 * The same timer as gyro_timer_start(), with one difference: it fires first
 * after @p first milliseconds and then, for as long as the callback keeps
 * returning GYRO_CB_CONTINUE, every @p period. Everything else holds, the
 * single token across firings, the re-arm counted from the loop's clock at
 * the moment it fired, the stop by returning GYRO_CB_SUCCESS or by
 * gyro_request_cancel().
 *
 * gyro_timer_start() with GYRO_CB_CONTINUE already repeats; what it cannot
 * express is "start in five seconds, then every one", because it re-arms for
 * the interval it was given. This is that, and nothing more: a periodic timer
 * that wants the first firing after one period is gyro_timer_start() with the
 * period as its timeout.
 *
 * @param first Milliseconds before the first firing. 0 means the next turn.
 * @param period Milliseconds between firings after the first. Must be
 *             positive: a period of 0 would have the loop fire the timer on
 *             every turn without ever sleeping, which is not a timer but a
 *             busy loop, and is refused.
 * @param cb As for gyro_timer_start(): NULL `handle`, 0 `transferred`,
 *         GYRO_COMPLETED or GYRO_ECANCELED, and a return value consulted only
 *         after a firing.
 * @param data Passed back to @p cb untouched.
 * @param out_token Receives the token naming the timer, or NULL. It is the
 *                same for every firing.
 * @return GYRO_PENDING once the loop owns the timer, or a negative status:
 *       GYRO_EINVAL for a negative @p first, a non-positive @p period or a
 *       NULL @p cb, GYRO_ENOMEM when the request store could not provide a
 *       slot. Never GYRO_COMPLETED.
 *
 * @note Thread-safe: a timer armed from elsewhere is handed to the loop and
 *       fires there.
 */
GYRO_API int gyro_timer_start_periodic(gyro_t *gyro, long long first, long long period, gyro_rq_user_cb cb,
                                       void *data, gyro_request_t *out_token);

#ifdef __cplusplus
}
#endif

#endif // !GYRO_TIMER_H_
