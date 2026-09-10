// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_ERROR_H_
#define GYRO_ERROR_H_

#include <gyro/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Outcome of an operation, in a numeric space of its own.
 *
 * Platform codes are normalised onto these, so a host runtime needs one
 * translation table rather than one per operating system. Values are part of
 * the ABI: new ones may be appended, existing ones never change.
 *
 * Functions return int rather than this type, so that a code outside the
 * enumeration can still travel.
 *
 * There is deliberately no equivalent of EAGAIN or EINTR: "try again later"
 * and interrupted syscalls are absorbed by the loop, and the caller only ever
 * sees final outcomes.
 */
typedef enum {
    /* Non-negative outcomes */
    GYRO_COMPLETED = 0, /* satisfied at once, or the loop ran out of work */
    GYRO_PENDING = 1, /* the callback will follow */
    GYRO_STOPPED = 2, /* the loop returned because it was asked to */

    /* Generic */
    GYRO_EUNKNOWN = -1, /* platform code with no mapping */
    GYRO_EINVAL = -2, /* invalid argument */
    GYRO_ENOMEM = -3, /* allocation hook returned NULL, or the store is capped */
    GYRO_ENOTSUP = -4, /* unsupported by this handle or backend */
    GYRO_EBUSY = -5, /* still in use, and releasing it now would lose something */

    /* Lifecycle */
    GYRO_ECANCELED = -10, /* cancelled explicitly, or by closing the handle */
    GYRO_ETIMEDOUT = -11, /* the deadline fired */
    GYRO_EBADF = -12, /* submitted on a closing handle */

    /* I/O */
    GYRO_EOF = -20, /* the peer shut down cleanly: an event, not a failure */
    GYRO_ECONNRESET = -21,
    GYRO_ECONNREFUSED = -22,
    GYRO_ECONNABORTED = -23,
    GYRO_EPIPE = -24,
    GYRO_EHOSTUNREACH = -25,
    GYRO_ENETUNREACH = -26,

    /* Resources and addresses */
    GYRO_EADDRINUSE = -30,
    GYRO_EADDRNOTAVAIL = -31,
    GYRO_EMFILE = -32, /* out of descriptors, typical under a burst of accepts */
    GYRO_EACCES = -33,
} gyro_errno_t;

/**
 * @brief Returns a human-readable description of a code.
 *
 * Never returns NULL: an unrecognised code yields a generic description.
 *
 * @note Thread-safe: it reads a table that never changes.
 */
GYRO_API const char *gyro_strerror(int code);

/**
 * @brief Returns the symbolic name of a code, such as "GYRO_ECANCELED".
 *
 * Meant for logs, and for a host runtime deriving its own error or exception
 * names from gyro's; gyro_strerror() provides the wording for a message.
 *
 * @note Thread-safe: it reads a table that never changes.
 */
GYRO_API const char *gyro_err_name(int code);

#ifdef __cplusplus
}
#endif

#endif // !GYRO_ERROR_H_
