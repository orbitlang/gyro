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

/// Lets other sockets bind the same address, as SO_REUSEADDR does.
#define GYRO_TCP_REUSEADDR 0x01

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
 * @brief Binds a TCP handle to a specified address.
 *
 * This function associates a TCP handle with a specified address and port for future
 * operations, such as listening for incoming connections. The address provided must
 * match the requirements of the underlying platform and protocol family.
 *
 * The function performs various checks, including ensuring that the TCP handle is
 * in an active state and the provided address and length parameters are valid.
 * Additionally, it optionally enables the reuse of local addresses based on the
 * flags provided.
 *
 * @param tcp A pointer to a gyro_tcp_t instance representing the TCP handle to bind.
 * @param addr A pointer to a sockaddr structure specifying the address to bind the handle to.
 * @param addrlen The size of the address structure.
 * @param flags A set of flags controlling the binding behavior. The GYRO_TCP_REUSEADDR
 *              flag enables reuse of local addresses.
 * @return GYRO_COMPLETED on success, or a negative gyro_errno_t value indicating the error.
 *         Errors include GYRO_EINVAL for invalid arguments, GYRO_EBADF if the handle is
 *         not active, and other platform-specific errors converted to gyro codes.
 */
GYRO_API int gyro_tcp_bind(gyro_tcp_t *tcp, const struct sockaddr *addr, size_t addrlen, unsigned int flags);

/**
 * @brief Listens for incoming TCP connections on the given socket.
 *
 * This function enables the socket to accept incoming connection requests,
 * setting it into a listening state.
 *
 * @param tcp A pointer to a `gyro_tcp_t` structure representing the socket.
 *            The socket must have been initialized and bound to an address before calling this function.
 * @param backlog The maximum length of the queue of pending connections. This value defines
 *                how many connection requests can be queued before connections are refused.
 * @return
 *         - GYRO_COMPLETED on success.
 *         - GYRO_EINVAL if the provided tcp is null or if the socket handle is invalid.
 *         - GYRO_EBADF if the socket is not in an active state.
 *         - A specific negative error code corresponding to platform error if `listen` fails.
 */
GYRO_API int gyro_tcp_listen(const gyro_tcp_t *tcp, int backlog);

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
