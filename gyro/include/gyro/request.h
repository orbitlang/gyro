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

typedef gyro_cb_status_t (*gyro_rq_op_cb)(gyro_handle_t *handle, gyro_op_t *op);

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
 * Doing nothing counts as success, so this is safe to call unconditionally;
 * on an operation that has already finished, on a stale token, or on an
 * invalid one.
 */
GYRO_API int gyro_request_cancel(gyro_t *gyro, gyro_request_t token);

#ifdef __cplusplus
}
#endif

#endif // !GYRO_REQUEST_H_
