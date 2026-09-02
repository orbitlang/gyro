// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_TCP_H_
#define GYRO_TCP_H_

#include <stddef.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <ws2def.h>
#else
#include <sys/socket.h>
#endif

#include <gyro/export.h>
#include <gyro/loop.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A TCP connection, or a socket listening for them.
 *
 * Upcasts to gyro_handle_t with GYRO_HANDLE(), which is how it is closed and
 * how user data is attached to it.
 */
typedef struct GyroTcp gyro_tcp_t;

/**
 * @brief Creates a TCP handle owned by the loop.
 *
 * No socket exists yet: the address family is only known at bind or connect
 * time, so the descriptor is opened there. A handle that never gets that far
 * is still closed with gyro_close().
 *
 * @return The handle, or NULL if the allocator refused.
 */
GYRO_API gyro_tcp_t *gyro_tcp_new(gyro_t *gyro);

#ifdef __cplusplus
}
#endif

#endif // !GYRO_TCP_H_
