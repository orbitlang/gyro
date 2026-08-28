// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_PLATFORM_OSTYPES_H_
#define GYRO_PLATFORM_OSTYPES_H_

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <winsock2.h>
#endif

namespace gyro {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    using OSPoll = void *;   ///< HANDLE of the completion port.
    using OSSocket = UINT_PTR; ///< SOCKET. Pointer-sized, so 64 bit on Win64.

    /// CreateIoCompletionPort() reports failure with NULL, not INVALID_HANDLE_VALUE.
    inline constexpr OSPoll kInvalidPoll = nullptr;

    /// Zero is a legal socket, so the sentinel is INVALID_SOCKET, that is ~0.
    inline constexpr OSSocket kInvalidSocket = INVALID_SOCKET;
#else
    using OSPoll = int;   ///< Descriptor of the kqueue or epoll instance.
    using OSSocket = int; ///< Descriptor of the socket.

    inline constexpr OSPoll kInvalidPoll = -1;
    inline constexpr OSSocket kInvalidSocket = -1;
#endif
} // namespace gyro

#endif // !GYRO_PLATFORM_OSTYPES_H_
