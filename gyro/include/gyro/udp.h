// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_UDP_H_
#define GYRO_UDP_H_

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
#define GYRO_UDP_REUSEADDR 0x01

/**
 * @file udp.h
 *
 * Datagrams, and the ways they are not a stream. The shape of the API is the
 * one tcp.h uses, down to the three return values and the single callback, but
 * four of the promises underneath it are different and none of them is a
 * detail:
 *
 * - **A datagram arrives whole or not at all.** A receive is never short the
 *   way a TCP read is: it hands over exactly one datagram. If it does not fit
 *   in the regions offered, the remainder is **discarded by the kernel** and
 *   the operation reports GYRO_EMSGSIZE. Reading again does not recover it.
 * - **A send is all or nothing.** There are no partial datagrams, so there is
 *   no remainder to retry and nothing is ever written in part.
 * - **There is no end of stream.** GYRO_EOF never arrives, and a datagram of
 *   length zero is a real datagram that reports GYRO_COMPLETED with zero bytes
 *   transferred.
 * - **Delivery is the network's business, not gyro's.** A send that reports
 *   GYRO_COMPLETED means the datagram left this machine, not that anybody
 *   received it, and nothing will ever say otherwise.
 *
 * Everything the loop gives a TCP handle it gives this one too: the same
 * tokens, the same deadlines, the same cancellation, the same ordered close,
 * the same fast path that serves an operation on the calling thread when
 * nothing else is outstanding in that direction, and the same per-direction
 * order for the ones that are not.
 *
 * **One rule covers everything handed to an operation**, and the addresses are
 * not an exception to it: the regions, the address a send is aimed at and the
 * one a receive fills in all have to stay valid, and unmodified, until the
 * operation reports. An operation the loop takes over keeps the pointers and
 * uses them later, so anything with a shorter life than the operation works
 * only while the fast path keeps winning. Note that gyro_udp_bind() and
 * gyro_udp_connect() are the other way round: they finish inside the call, so
 * what they are given is read and done with before they return.
 *
 * There is no accept and no listen: a UDP socket has no connections to accept.
 * gyro_udp_connect() exists, but it does something much smaller than its TCP
 * namesake.
 *
 * **This is an IP socket**, AF_INET or AF_INET6, and every call that takes a
 * family or an address refuses anything else. AF_UNIX datagram sockets are a
 * real thing and they are not this one: what is written above about the network
 * does not hold for them, nor does what GYRO_EMSGSIZE means, so they belong to
 * a handle type of their own rather than to a pile of exceptions here. The
 * machinery underneath would be shared, not the contract.
 *
 * Multicast, broadcast, time to live, SO_REUSEPORT and the rest have no calls
 * of their own: open the socket with gyro_udp_open(), set what you need through
 * gyro_udp_fileno(), then bind or send.
 *
 * Every entry point here is callable from any thread, on the same terms as
 * tcp.h: what is left to the caller is the ordering between setting a handle up
 * and using it.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A UDP socket.
 *
 * Upcasts to gyro_handle_t with GYRO_HANDLE(), which is how it is closed and
 * how user data is attached to it.
 */
typedef struct GyroUdp gyro_udp_t;

/**
 * @brief Binds the socket to a local address.
 *
 * Needed to receive anything: a socket nobody can address receives nothing. A
 * socket that only sends can skip it, and the kernel picks a port on the first
 * send.
 *
 * Opens the descriptor if it does not exist yet: the address is what tells gyro
 * which family to open it for.
 *
 * @param addr Local address to take. Its family must be AF_INET or AF_INET6,
 *           and is what the descriptor is opened for when there is not one yet.
 * @param flags Zero, or GYRO_UDP_REUSEADDR.
 * @return GYRO_COMPLETED, or a negative status: GYRO_EINVAL for an address of
 *       any other family.
 *
 * @note Thread-safe, and synchronous wherever it is called: the answer is the
 *       kernel's and comes back at once.
 */
GYRO_API int gyro_udp_bind(gyro_udp_t *udp, const struct sockaddr *addr, size_t addrlen, unsigned int flags);

/**
 * @brief Fixes the peer this socket talks to.
 *
 * Nothing is negotiated and nothing travels: UDP has no handshake, so unlike
 * gyro_tcp_connect() this returns the outcome at once, takes no callback and
 * has no token. What it changes is three things on the socket: a send may omit
 * the destination, datagrams from anybody else are dropped before they reach
 * the application, and the ICMP errors the network reports for this peer start
 * being delivered, so a receive can report GYRO_ECONNREFUSED where before it
 * would have waited for ever.
 *
 * Opens the descriptor if it does not exist yet. May be called again to point
 * the socket at a different peer.
 *
 * **There is no way back.** POSIX dissolves the association by connecting to
 * an address of family AF_UNSPEC, and that is deliberately refused here: the
 * systems disagree on what it reports. Darwin dissolves the association and
 * then answers with an error, Linux usually answers with success, so one call
 * would mean two things depending on where it ran. A caller who needs it, and
 * is willing to own that, can reach the socket through gyro_udp_fileno().
 *
 * @param addr Peer to talk to. Only read for the duration of the call, since
 *           the kernel is given it before this returns and nothing is queued.
 *           Its family must be AF_INET or AF_INET6.
 * @return GYRO_COMPLETED, or a negative status: GYRO_EINVAL for an address of
 *       any other family, AF_UNSPEC included.
 *
 * @note Thread-safe, and synchronous.
 */
GYRO_API int gyro_udp_connect(gyro_udp_t *udp, const struct sockaddr *addr, size_t addrlen);

/**
 * @brief Opens the descriptor without doing anything with it yet.
 *
 * The moment the options a kernel only honours before a socket is bound need:
 * joining a multicast group, SO_REUSEPORT, SO_BROADCAST, buffer sizes,
 * IPV6_V6ONLY. Set them through gyro_udp_fileno(), then bind or send as usual.
 *
 * Idempotent. A handle that already has a descriptor is left exactly as it is.
 *
 * @param family Address family, AF_INET or AF_INET6. Binding or connecting an
 *             address of another family afterwards is refused by the OS rather
 *             than by gyro.
 * @return GYRO_COMPLETED, or a negative status.
 *
 * @warning gyro has already set what it needs on that descriptor: non-blocking
 * mode, close-on-exec, and SO_NOSIGPIPE where it exists. Non-blocking is not
 * negotiable; clearing it stalls the loop on the first operation that has to
 * wait.
 *
 * @note Thread-safe.
 */
GYRO_API int gyro_udp_open(gyro_udp_t *udp, int family);

/**
 * @brief Takes the next datagram.
 *
 * Exactly one, whole. The regions are filled in order, and what does not fit
 * is lost: the kernel discards the rest of the datagram and the operation
 * reports GYRO_EMSGSIZE with the bytes that did fit. There is no reading the
 * remainder afterwards, so a receiver sizes its regions for the largest
 * datagram it is willing to accept.
 *
 * Receives on one handle are served in the order they were submitted, so a
 * second receive takes the datagram behind the first one's.
 *
 * @param bufs Regions to fill, in order. They and the array naming them must
 *           stay valid, and unmodified, until the operation reports.
 * @param from Receives the sender's address, or NULL when it does not matter,
 *           which is the usual case on a connected socket. Written when the
 *           operation reports, so like @p bufs it must stay valid until then.
 * @param fromlen On the way in, the room @p from offers; on the way out, the
 *              length actually written. Required when @p from is given, and
 *              ignored when it is NULL.
 * @param timeout Milliseconds before the receive is given up on, or 0 for
 *              none. A deadline that arrives reports GYRO_ETIMEDOUT, distinct
 *              from the GYRO_ECANCELED of a cancellation somebody asked for.
 * @param cb Reports the outcome. Not called when GYRO_COMPLETED is returned.
 * @param token Receives the token naming the operation, or NULL. Set to an
 *            invalid token unless GYRO_PENDING is returned.
 * @param transferred Receives the byte count when a datagram was already
 *                  waiting, or NULL. Set to 0 in every other case, which for a
 *                  datagram of length zero is also the true answer.
 * @return GYRO_PENDING, GYRO_COMPLETED when a datagram was already waiting, or
 *       a negative status. Never GYRO_EOF: a UDP socket has no end.
 *
 * @note Thread-safe: the receive may be carried out on the calling thread when
 *       nothing else is outstanding in that direction, and is otherwise handed
 *       to the loop and reported later.
 */
GYRO_API int gyro_udp_recv(gyro_udp_t *udp, gyro_buf_t *bufs, unsigned int nbufs, struct sockaddr *from,
                           size_t *fromlen, long long timeout, gyro_rq_user_cb cb, void *data,
                           gyro_request_t *token, size_t *transferred);

/**
 * @brief Sends one datagram.
 *
 * The regions are gathered into a single datagram, which leaves whole or does
 * not leave at all. A send never reports part of one, and the retry loop a TCP
 * write needs has nothing to do here.
 *
 * GYRO_COMPLETED means the datagram was handed to the network, and no more
 * than that. Whether it arrived, arrived once, or arrived in the order it was
 * sent are not questions UDP answers, and gyro does not pretend to answer them
 * either.
 *
 * Sends on one handle leave in the order they were submitted.
 *
 * @param bufs Regions to send, in order. They and the array naming them must
 *           stay valid, and unmodified, until the operation reports.
 * @param to Where to send it, or NULL to use the peer set by
 *         gyro_udp_connect(). Like the regions, and for the same reason, it
 *         must stay valid until the operation reports: a send the loop takes
 *         over keeps the pointer and reads it when the socket has room. An
 *         address on the stack therefore works whenever the send goes out at
 *         once and fails when it does not, which is the worst way for it to
 *         fail; give it the lifetime of the operation, as you would a buffer.
 * @param tolen Length of @p to, and ignored when it is NULL.
 * @param timeout Milliseconds before the send is given up on, or 0 for none.
 *              Rarely of use: a send waits only for room in the socket's own
 *              buffer, never for the network.
 * @param cb Reports the outcome. Not called when GYRO_COMPLETED is returned.
 * @param token Receives the token naming the operation, or NULL. Set to an
 *            invalid token unless GYRO_PENDING is returned.
 * @param transferred Receives the byte count when the datagram went out at
 *                  once, or NULL. Set to 0 in every other case.
 * @return GYRO_PENDING, GYRO_COMPLETED when the datagram went out at once,
 *       which is the normal case, or a negative status such as GYRO_EMSGSIZE
 *       for a datagram larger than the path will carry.
 *
 * @note Thread-safe: the send may be carried out on the calling thread when
 *       nothing else is outstanding in that direction, and is otherwise handed
 *       to the loop and reported later.
 */
GYRO_API int gyro_udp_send(gyro_udp_t *udp, gyro_buf_t *bufs, unsigned int nbufs, const struct sockaddr *to,
                           size_t tolen, long long timeout, gyro_rq_user_cb cb, void *data,
                           gyro_request_t *token, size_t *transferred);

/**
 * @brief Returns the socket the handle is built on.
 *
 * For what gyro does not wrap: joining a multicast group, SO_BROADCAST, the
 * time to live, or a question only the OS can answer, getsockname() on a socket
 * bound to port 0 being the usual one.
 *
 * Do not read from it, write to it, or close it. The loop owns the readiness of
 * that socket and the operations queued against it: receiving behind its back
 * takes a datagram belonging to a queued receive, and closing it hands the
 * number back to the OS while the loop is still watching it. Use
 * gyro_handle_close() instead.
 *
 * @return The socket, or GYRO_INVALID_SOCKET while the handle has none, which
 *       is the case until gyro_udp_bind(), gyro_udp_connect() or
 *       gyro_udp_open() opens one.
 *
 * @note Thread-safe, with the caveat every handle carries: the number is only
 *       meaningful while the handle is open, so asking for it while somebody
 *       else is closing the handle is undefined.
 */
GYRO_API gyro_socket_t gyro_udp_fileno(const gyro_udp_t *udp);

/**
 * @brief Creates a UDP handle owned by the loop.
 *
 * No socket exists yet: the address family is only known at bind, connect or
 * open time, so the descriptor is opened there. A handle that never gets that
 * far is still closed with gyro_handle_close().
 *
 * @return The handle, or NULL if the allocator refused.
 *
 * @note Thread-safe as far as gyro is concerned: the only thing it reaches for
 *       is the loop's allocator, which is already documented as callable from
 *       any thread that calls into gyro.
 */
GYRO_API gyro_udp_t *gyro_udp_new(gyro_t *gyro);

#ifdef __cplusplus
}
#endif

#endif // !GYRO_UDP_H_
