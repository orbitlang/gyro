// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_NET_H_
#define GYRO_NET_H_

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

#endif // !GYRO_NET_H_
