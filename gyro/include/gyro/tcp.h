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
#include <gyro/request.h>

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
 * @brief Takes the next incoming connection.
 *
 * The connection lands in @p client, which the caller creates beforehand with
 * gyro_tcp_new() and owns from then on — including when the operation fails, in
 * which case @p client is untouched and can be reused or closed. Handing it in
 * rather than receiving it back is what lets the same call work on a completion
 * port, where the socket has to exist before the accept is posted, and it keeps
 * the callback signature the same as every other operation's.
 *
 * @p client must stay alive until the operation reports, exactly as a buffer
 * would.
 *
 * @param timeout Milliseconds to wait for a connection, or 0 to wait
 *                indefinitely.
 * @param cb Reports the outcome. Its `transferred` is always 0.
 * @param out_token Receives the token naming the operation, or NULL.
 * @return GYRO_PENDING, GYRO_COMPLETED if a connection was already waiting, or
 *         a negative status.
 */
GYRO_API int gyro_tcp_accept(gyro_tcp_t *tcp, gyro_tcp_t *client, long long timeout,
                             gyro_rq_user_cb cb, void *data, gyro_request_t *out_token);

/**
 * @brief Binds the socket to a local address.
 *
 * Opens the descriptor if it does not exist yet: the address is what tells gyro
 * which family to open it for.
 *
 * @param flags Zero, or GYRO_TCP_REUSEADDR.
 * @return GYRO_COMPLETED, or a negative status.
 */
GYRO_API int gyro_tcp_bind(gyro_tcp_t *tcp, const struct sockaddr *addr, size_t addrlen, unsigned int flags);

/**
 * @brief Connects to a peer.
 *
 * Opens the descriptor if it does not exist yet, which is the usual case: a
 * handle straight from gyro_tcp_new() has no socket, and the address is what
 * says which family to open it for.
 *
 * @param addr Peer to reach. Only read for the duration of the call.
 * @param timeout Milliseconds to wait, or 0 to wait as long as the OS does.
 *                A connection is not established any faster by giving up on it
 *                sooner, so this is about how long the caller is prepared to
 *                block, not about the network.
 * @param cb Reports the outcome. Its `transferred` is always 0.
 * @param out_token Receives the token naming the operation, or NULL. Set to an
 *                  invalid token unless GYRO_PENDING is returned.
 * @return GYRO_PENDING, GYRO_COMPLETED when the connection was established at
 *         once — which happens over loopback — or a negative status such as
 *         GYRO_ECONNREFUSED.
 */
GYRO_API int gyro_tcp_connect(gyro_tcp_t *tcp, const struct sockaddr *addr, size_t addrlen, long long timeout,
                              gyro_rq_user_cb cb, void *data, gyro_request_t *out_token);

/**
 * @brief Starts accepting connections on a bound socket.
 *
 * Only marks the socket as listening; connections are taken one at a time with
 * gyro_tcp_accept(). Synchronous, because the kernel answers at once.
 *
 * @param backlog How many connections the kernel may hold before refusing more.
 * @return GYRO_COMPLETED, or a negative status.
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
