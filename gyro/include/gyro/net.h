// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_NET_H_
#define GYRO_NET_H_

#include <gyro/export.h>

/**
 * @file net.h
 *
 * What the network handles have in common, and only that: anything shaped by
 * one protocol belongs to that protocol's header.
 *
 * The bind flags live here because the options behind them are the socket's
 * and not the protocol's: one set of flags serves every gyro_*_bind(). They
 * still do not mean the same thing everywhere, because the protocols do not
 * use an address the same way, and each bind says what a flag means for it.
 *
 * A bind refuses a bit it does not know with GYRO_EINVAL rather than ignoring
 * it, so a flag is never silently dropped. Which flags there are is asked in
 * two places, and they can disagree: whether the header a program was built
 * with knows a flag is the preprocessor's question, since each one is a
 * macro, and `#ifdef` answers it; whether the library it runs against does is
 * gyro_bind_flags_supported()'s.
 */

/**
 * @brief Lets the address be taken again while it is still held, as
 *        SO_REUSEADDR does.
 *
 * What "held" means is the protocol's: for TCP it is the connections of a
 * previous run still winding down, for UDP it is another socket bound to the
 * same port. See gyro_tcp_bind() and gyro_udp_bind().
 */
#define GYRO_BIND_REUSEADDR 0x01

/**
 * @brief Keeps an IPv6 socket to IPv6, as IPV6_V6ONLY does.
 *
 * Without it, an IPv6 socket bound to the unspecified address may also take
 * IPv4 traffic, seen as IPv4-mapped addresses (::ffff:a.b.c.d), and whether it
 * does is the system's default rather than gyro's: Linux and Darwin say yes,
 * FreeBSD and Windows say no, OpenBSD never allows it. Give this flag and the
 * answer is no everywhere, which is what a program serving both families on
 * one port with two sockets needs, since the IPv6 one would otherwise take the
 * port from the IPv4 one.
 *
 * Only for an AF_INET6 address: an IPv4 bind given it is refused with
 * GYRO_EINVAL, since there is nothing for it to do and the caller has
 * evidently mixed something up. Leaving it out does not set the option back:
 * the descriptor keeps whatever it already says, the system default or what
 * the caller set through the handle's fileno after opening it.
 */
#define GYRO_BIND_IPV6ONLY 0x02

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tells whether every flag in @p flags is one this library knows.
 *
 * Answered by the library that is running, not by the header the caller was
 * built with, which is the point: a program built against a newer header and
 * run against an older shared library can ask before it binds, and fall back
 * rather than fail. A mask in the header could not do that, since it would be
 * compiled into the caller and describe the header instead.
 *
 * Says that the flags are known, not that a given bind will take them:
 * GYRO_BIND_IPV6ONLY is known and still refused on an IPv4 address, and the
 * kernel has its own say once the bind runs.
 *
 * @param flags Any combination of GYRO_BIND_* bits. Zero is valid.
 * @return Non-zero when every bit is known, zero when at least one is not.
 *
 * @note Thread-safe: it reads a constant.
 */
GYRO_API int gyro_bind_flags_supported(unsigned int flags);

#ifdef __cplusplus
}
#endif

#endif // !GYRO_NET_H_
