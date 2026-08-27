// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_BUF_H_
#define GYRO_BUF_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One region of memory taking part in an operation.
 *
 * An operation is described by an array of these, so a header and a payload
 * living apart still travel in a single syscall: on a write the kernel gathers
 * them into one stream, on a read the incoming bytes fill the first region and
 * spill into the next.
 *
 * The memory belongs to the caller and gyro never copies it. Both the regions
 * and the array describing them must stay valid, and unmodified, until the
 * operation reports.
 *
 * **The field order deliberately differs between platforms**, so that the
 * caller's array is layout-compatible with the one the syscall expects and can
 * be handed to it with a cast, with no conversion and no temporary.
 * The field names are the same everywhere, so callers do not notice.
 */
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
typedef struct gyro_buf {
    unsigned long len;
    char *base;
} gyro_buf_t;
#else
typedef struct gyro_buf {
    char *base;
    size_t len;
} gyro_buf_t;
#endif

#ifdef __cplusplus
}
#endif

#endif // !GYRO_BUF_H_
