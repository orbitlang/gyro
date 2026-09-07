// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_OS_H_
#define GYRO_OS_H_

#include <stdint.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
/// SOCKET: pointer-sized, so 64 bit on Win64, and unsigned.
typedef uintptr_t gyro_socket_t;

/// Zero is a legal socket on Windows, so the sentinel is ~0, as INVALID_SOCKET.
#define GYRO_INVALID_SOCKET ((gyro_socket_t) ~(uintptr_t) 0)
#else
/// Descriptor of the socket.
typedef int gyro_socket_t;

#define GYRO_INVALID_SOCKET ((gyro_socket_t) -1)
#endif

#endif // !GYRO_OS_H_
