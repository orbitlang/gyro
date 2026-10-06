// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_TCP_H_
#define GYRO_TCP_H_

#include <stddef.h>

#include <gyro/os.h>
#include <gyro/platform.h>

#if GYRO_OS_WINDOWS
#include <ws2def.h>
#else
#include <sys/socket.h>
#endif

#include <gyro/buf.h>
#include <gyro/export.h>
#include <gyro/loop.h>
#include <gyro/request.h>

/// Lets other sockets bind the same address, as SO_REUSEADDR does.
#define GYRO_TCP_REUSEADDR 0x01

/**
 * @file tcp.h
 *
 * A byte stream, and the two halves of it are independent: a read and a write
 * on one connection neither wait for nor block each other, and each has its
 * own order. Two promises are worth stating before anything else, because they
 * are what the calls below are shaped around:
 *
 * - **A read is short.** It reports as soon as one byte has arrived, up to the
 *   room offered, and never waits for more. Reading a known number of bytes is
 *   a layer on top, built by reading again.
 *   GYRO_EOF says the peer shut down cleanly, and is the only way to learn
 *   that no more bytes are coming.
 * - **A write is complete.** A partial write is not an outcome: the loop
 *   retries the remainder itself and reports only once the last byte has gone,
 *   so GYRO_COMPLETED never means part of it and the caller never writes a
 *   retry loop.
 *
 * **One rule covers everything handed to an operation**: the regions, the array
 * naming them, and the handle an accept is told to land a connection in all
 * have to stay valid, and unmodified, until the operation reports. An operation
 * the loop takes over keeps the pointers and uses them later, so anything with
 * a shorter life than the operation works only while the fast path keeps
 * winning. gyro_tcp_bind() and gyro_tcp_connect() are the other way round:
 * they finish inside the call, so the address they are given is read and done
 * with before they return.
 *
 * **This is an IP socket**, AF_INET or AF_INET6, and every call that takes a
 * family or an address refuses anything else.
 *
 * Socket options, keepalive, TCP_NODELAY and the rest have no calls of their
 * own: open the socket with gyro_tcp_open(), set what you need through
 * gyro_tcp_fileno(), then bind or connect.
 *
 * Every entry point here is callable from any thread. The ones that set a
 * handle up, gyro_tcp_new(), gyro_tcp_open(), gyro_tcp_bind(),
 * gyro_tcp_listen() and gyro_tcp_connect(), touch nothing the loop owns: they
 * work on the handle's own descriptor, and the loop never looks at it until an
 * operation is submitted. What is left to the caller is the ordering between
 * setting a handle up and using it, which is the same rule that already governs
 * two threads working one handle: gyro does not serialise them, and does not
 * pretend to.
 */

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
 * gyro_tcp_new() and owns from then on, including when the operation fails; in
 * that case @p client is untouched and can be reused or closed. Handing it in
 * rather than receiving it back is what lets the same call work on a completion
 * port, where the socket has to exist before the accept is posted, and it keeps
 * the callback signature the same as every other operation's.
 *
 * @p client must stay alive until the operation reports, exactly as a buffer
 * would.
 *
 * @param timeout Milliseconds to wait for a connection, or 0 to wait
 *              indefinitely. A deadline that arrives reports
 *              GYRO_ETIMEDOUT, which is how it is told apart from a
 *              cancellation somebody asked for.
 * @param cb Reports the outcome. Its `transferred` is always 0.
 * @param out_token Receives the token naming the operation, or NULL.
 * @return GYRO_PENDING, GYRO_COMPLETED if a connection was already waiting, or
 *       a negative status.
 *
 * @note Thread-safe: a connection already waiting may be taken on the calling
 *       thread, and the accept is otherwise handed to the loop and reported
 *       later.
 */
GYRO_API int gyro_tcp_accept(gyro_tcp_t *tcp, gyro_tcp_t *client, long long timeout,
                             gyro_rq_user_cb cb, void *data, gyro_request_t *out_token);

/**
 * @brief Binds the socket to a local address.
 *
 * Opens the descriptor if it does not exist yet: the address is what tells gyro
 * which family to open it for.
 *
 * @param addr Local address to take. Its family must be AF_INET or AF_INET6,
 *           and is what the descriptor is opened for when there is not one yet.
 * @param flags Zero, or GYRO_TCP_REUSEADDR.
 * @return GYRO_COMPLETED, or a negative status: GYRO_EINVAL for an address of
 *       any other family.
 *
 * @note Thread-safe, and synchronous wherever it is called: the answer is the
 *       kernel's and comes back at once. Binding a handle somebody else is
 *       already using is the caller's race, not gyro's.
 */
GYRO_API int gyro_tcp_bind(gyro_tcp_t *tcp, const struct sockaddr *addr, size_t addrlen, unsigned int flags);

/**
 * @brief Connects to a peer.
 *
 * Opens the descriptor if it does not exist yet, which is the usual case: a
 * handle straight from gyro_tcp_new() has no socket, and the address is what
 * says which family to open it for.
 *
 * @param addr Peer to reach. Only read for the duration of the call. Its
 *           family must be AF_INET or AF_INET6.
 * @param timeout Milliseconds to wait, or 0 to wait as long as the OS does.
 *              A connection is not established any faster by giving up on it
 *              sooner, so this is about how long the caller is prepared to
 *              block, not about the network. A deadline that arrives reports
 *              GYRO_ETIMEDOUT.
 * @param cb Reports the outcome. Its `transferred` is always 0.
 * @param out_token Receives the token naming the operation, or NULL. Set to an
 *                invalid token unless GYRO_PENDING is returned.
 * @return GYRO_PENDING, GYRO_COMPLETED when the connection was established at
 *       once (which happens over loopback), or a negative status such as
 *       GYRO_ECONNREFUSED, or GYRO_EINVAL for an address of another family.
 *
 * @note Thread-safe.
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
 *
 * @note Thread-safe, and synchronous: the kernel answers at once.
 */
GYRO_API int gyro_tcp_listen(const gyro_tcp_t *tcp, int backlog);

/**
 * @brief Opens the descriptor without doing anything with it yet.
 *
 * gyro_tcp_bind() and gyro_tcp_connect() open one themselves, taking the
 * family from the address they are given, which leaves no moment in between
 * to configure it. This is that moment, for the options a kernel only honours
 * on a socket that is not yet bound or connected: binding to an interface,
 * IPV6_V6ONLY, buffer sizes, anything gyro has no call of its own for. Set
 * them through gyro_tcp_fileno(), then bind or connect as usual.
 *
 * Idempotent. A handle that already has a descriptor, from an earlier call or
 * from having been accepted, is left exactly as it is.
 *
 * @param family Address family, AF_INET or AF_INET6. Binding or connecting an
 *             address of another family afterwards is refused by the OS
 *             rather than by gyro.
 * @return GYRO_COMPLETED, or a negative status.
 *
 * @warning gyro has already set what it needs on that descriptor: non-blocking
 * mode, close-on-exec, and SO_NOSIGPIPE where it exists. Non-blocking is not
 * negotiable; clearing it stalls the loop on the first operation that has to
 * wait.
 *
 * @note Thread-safe.
 */
GYRO_API int gyro_tcp_open(gyro_tcp_t *tcp, int family);

/**
 * @brief Reads whatever has arrived.
 *
 * Short by design: it reports as soon as one byte is there, up to the room the
 * regions offer, and never waits for them to fill. Reading a known number of
 * bytes is a layer on top, built by reading again. It is not something the loop
 * can do on the caller's behalf without holding data it has already been given.
 *
 * GYRO_EOF says the peer shut the connection down cleanly. It is an event, not
 * a failure, and it is the only way to learn that no more bytes are coming.
 *
 * Reads submitted from one thread are served in that order, so a second read
 * never takes bytes belonging to the first. Two threads reading one connection
 * is an application-level race, and which of them gets the earlier bytes is
 * undefined.
 *
 * @param bufs Regions to fill, in order. They and the array naming them must
 *           stay valid, and unmodified, until the operation reports.
 * @param timeout Milliseconds before the read is given up on, or 0 for none.
 *              A deadline that arrives reports GYRO_ETIMEDOUT, distinct from
 *              the GYRO_ECANCELED of a cancellation somebody asked for.
 * @param cb Reports the outcome. Not called when GYRO_COMPLETED is returned.
 * @param token Receives the token naming the operation, or NULL. Set to an
 *            invalid token unless GYRO_PENDING is returned.
 * @param transferred Receives the byte count when the read is satisfied at
 *                  once, or NULL. Set to 0 in every other case.
 * @return GYRO_PENDING, GYRO_COMPLETED when data was already waiting, or a
 *       negative status such as GYRO_EOF.
 *
 * @note Thread-safe: the read may be carried out on the calling thread when
 *       nothing else is outstanding in that direction, and is otherwise handed
 *       to the loop and reported later.
 */
GYRO_API int gyro_tcp_read(gyro_tcp_t *tcp, gyro_buf_t *bufs, unsigned int nbufs, long long timeout,
                           gyro_rq_user_cb cb, void *data, gyro_request_t *token, size_t *transferred);

/**
 * @brief Writes everything, however many syscalls that takes.
 *
 * A partial write is not an outcome: the loop retries the remainder on its own
 * and reports only once the last byte has gone, so the caller never writes a
 * retry loop and GYRO_COMPLETED never means "part of it". The bytes counted on
 * a failure or a timeout are those that did make it out.
 *
 * Writes submitted from one thread leave in the order they were submitted,
 * and each one leaves in one piece.
 *
 * Neither holds between threads. Two threads writing to one connection is an
 * application-level race: gyro does not order them, and a write that the
 * kernel only accepts in part can come out with the other thread's bytes in
 * the middle of it. Anything that cares has to serialise its own writers.
 *
 * @param bufs Regions to send, in order. They and the array naming them must
 *           stay valid, and unmodified, until the operation reports, gyro
 *           hands them to the kernel rather than copying them, and leaves the
 *           array exactly as it found it.
 * @param timeout Milliseconds before the write is given up on, or 0 for none.
 *              A write that times out has usually sent something already, and
 *              reports GYRO_ETIMEDOUT along with how much.
 * @param cb Reports the outcome. Not called when GYRO_COMPLETED is returned.
 * @param token Receives the token naming the operation, or NULL. Set to an
 *            invalid token unless GYRO_PENDING is returned.
 * @param transferred Receives the byte count when everything went out in one
 *                  go, or NULL. Set to 0 in every other case.
 * @return GYRO_PENDING, GYRO_COMPLETED when the whole of it was written at
 *       once, or a negative status such as GYRO_EPIPE.
 *
 * @note Thread-safe: the write may be carried out on the calling thread when
 *       nothing else is outstanding in that direction, and is otherwise handed
 *       to the loop and reported later.
 */
GYRO_API int gyro_tcp_write(gyro_tcp_t *tcp, gyro_buf_t *bufs, unsigned int nbufs, long long timeout,
                            gyro_rq_user_cb cb, void *data, gyro_request_t *token, size_t *transferred);

/**
 * @brief Returns the socket the handle is built on.
 *
 * For what gyro does not wrap: a socket option it has no call for, or a
 * question only the OS can answer (e.g. getsockname) on a server bound to port 0
 * being the usual one.
 *
 * Do not read from it, write to it, or close it. The loop owns the readiness
 * of that socket and the operations queued against it: reading behind its back
 * takes bytes belonging to a queued read, and closing it hands the number back
 * to the OS while the loop is still watching it, which is how an unrelated file
 * ends up being polled. Use gyro_handle_close() instead.
 *
 * @return The socket, or GYRO_INVALID_SOCKET while the handle has none, which
 *       is the case until gyro_tcp_bind(), gyro_tcp_connect() or
 *       gyro_tcp_open() opens one.
 *
 * @note Thread-safe.
 */
GYRO_API gyro_socket_t gyro_tcp_fileno(const gyro_tcp_t *tcp);

/**
 * @brief Creates a TCP handle owned by the loop.
 *
 * No socket exists yet: the address family is only known at bind or connect
 * time, so the descriptor is opened there. A handle that never gets that far
 * is still closed with gyro_close().
 *
 * @return The handle, or NULL if the allocator refused.
 *
 * @note Thread-safe.
 */
GYRO_API gyro_tcp_t *gyro_tcp_new(gyro_t *gyro);
#ifdef __cplusplus
}
#endif

#endif // !GYRO_TCP_H_
