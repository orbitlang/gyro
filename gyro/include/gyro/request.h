// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_REQUEST_H_
#define GYRO_REQUEST_H_

#include <stddef.h>
#include <stdint.h>

#include <gyro/export.h>
#include <gyro/handle.h>
#include <gyro/loop.h>

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief Names one submitted operation.
 *
 * A value, not a pointer: it owns nothing, can be copied freely and needs no
 * cleanup. It goes stale once the operation reports its outcome, and a stale
 * token is recognised as such rather than naming whatever took its place.
 */
typedef struct {
    uint64_t _opaque;
} gyro_request_t;

typedef struct GyroRequest gyro_op_t;

/**
 * @brief Carries the operation out, once the handle is ready for it.
 *
 * The extension point: this is what a handle type (gyro's own or a third
 * party's) supplies to say what its operations actually do. Invoked from the
 * loop's thread, on the oldest operation queued in its direction.
 *
 * Must either report through gyro_op_complete() and return anything but
 * GYRO_CB_RETRY, or leave the operation untouched and return GYRO_CB_RETRY to
 * say it would block. It may also be invoked while the submit that created the
 * operation is still running, and must not behave differently.
 */
typedef gyro_cb_status_t (*gyro_rq_op_cb)(gyro_handle_t *handle, gyro_op_t *op);

/**
 * @brief Reports the outcome to whoever asked for the operation.
 *
 * Runs exactly once per operation that reaches the loop, and never for one that
 * a submit already answered with GYRO_COMPLETED or an error. The operation is
 * already unlinked by then, so the handle it is given can be closed from here.
 *
 * @param status GYRO_COMPLETED, or a negative code such as GYRO_EOF.
 * @param transferred Bytes moved by the operation as a whole, including any
 *                    moved before it failed or was given up on.
 */
typedef gyro_cb_status_t (*gyro_rq_user_cb)(gyro_handle_t *handle, int status, size_t transferred, void *data);

/// Returns a token naming no operation.
static inline gyro_request_t gyro_request_invalid(void) {
    gyro_request_t token;

    token._opaque = 0;

    return token;
}

/// True while the token still names a live operation.
#define GYRO_REQUEST_IS_VALID(r) ((r)._opaque != 0)

/**
 * @brief Cancels a submitted operation.
 *
 * Asks for the cancellation and returns: the callback still fires, later, and
 * reports GYRO_ECANCELED. An operation that has already completed cannot be
 * taken back and reports its real outcome.
 *
 * What cancelling actually does is bring the operation's deadline forward to
 * now, so the loop reports it on its next turn rather than whenever the
 * operation would have finished on its own. An idle socket that was going to
 * keep a read waiting all day does not delay the callback, and neither does a
 * timer set for an hour from now.
 *
 * Doing nothing counts as success, so this is safe to call unconditionally;
 * on an operation that has already finished, on a stale token, or on an
 * invalid one. Cancelling twice is the same as cancelling once.
 *
 * The request is never touched from here: the token is handed to the loop,
 * which resolves it and acts on it in its own thread. That is also what
 * makes the order safe. A token exists only once its submit has returned,
 * and that submit had already handed the operation to the loop, so a
 * cancellation always reaches the loop behind the operation it names and
 * can never be acted on before the operation is there to be found.
 *
 * @return GYRO_COMPLETED, or GYRO_ENOMEM in the one case where nothing could
 *       be done: the note carrying the token to the loop has to come from the
 *       request store, and the store could not provide one.
 *
 * @note Thread-safe. From the loop's own thread it takes effect on the next
 *       turn, exactly as it does from anywhere else.
 */
GYRO_API int gyro_request_cancel(gyro_t *gyro, gyro_request_t token);

/**
 * @brief Submits an operation on a handle.
 *
 * The general form, for handle types gyro does not provide itself: the
 * operation is whatever @p cb_op does, and everything else (the queue it waits
 * in, the deadline, the token, the release when it is over) is the loop's.
 *
 * @p cb_op runs when the handle is ready in that direction, and must report
 * through gyro_op_complete() unless it returns GYRO_CB_RETRY to say it would
 * block. @p cb_user is what gyro_op_complete() then invokes, and is where the
 * caller learns the outcome.
 *
 * The caller must hold a claim on @p direction, taken with
 * gyro_handle_try_begin(), and this consumes it: the request carries it until
 * it reports, and a submit that fails before a request exists gives it back
 * itself. So a caller hands the claim over here and never touches it again,
 * whatever the outcome.
 *
 * @param data Passed back to @p cb_user untouched.
 * @param out_token Receives the token naming the operation. May be NULL when
 *                the caller will never cancel it.
 * @param timeout Milliseconds before the operation is given up on, or 0 to
 *              let it wait indefinitely. A deadline that arrives reports
 *              GYRO_ETIMEDOUT; a cancellation somebody asked for reports
 *              GYRO_ECANCELED, even when the operation also had a deadline.
 * @return GYRO_PENDING once the loop owns the operation, or a negative status,
 *       in which case no callback will fire.
 *
 * @note Thread-safe: a submit from elsewhere is handed to the loop and reports
 *       later. Attempting the operation on the calling thread first is the
 *       caller's to do, between the claim and this call.
 */
GYRO_API int gyro_request_submit(gyro_handle_t *handle, void *data, const gyro_rq_op_cb cb_op, const gyro_rq_user_cb cb_user,
                                 gyro_request_t *out_token, const long long timeout, const gyro_dir_t direction);

/**
 * @brief Reports the outcome of an operation and hands it back to the loop.
 *
 * Called by the operation callback once it knows what happened. The operation
 * is unlinked from its handle before the user callback runs, so user code can
 * never reach a request that is completing, and is released afterwards: the
 * pointer must not be used again.
 *
 * @param op The operation being reported. Invalid once this returns.
 * @param status GYRO_COMPLETED, or a negative code such as GYRO_EOF.
 * @param transferred Bytes moved by the operation as a whole.
 *
 * @note Loop-affine, and strictly: it unlinks from a handle queue, drops a
 *       timer, and returns a slot to the store. A driver that does its work
 *       on a thread of its own cannot report from there.
 */
GYRO_API void gyro_op_complete(gyro_op_t *op, int status, size_t transferred);

#ifdef __cplusplus
}
#endif

#endif // !GYRO_REQUEST_H_
