// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_PLATFORM_OSTYPES_H_
#define GYRO_PLATFORM_OSTYPES_H_

#include <gyro/os.h>

#if GYRO_OS_WINDOWS
#include <winsock2.h>
#endif

namespace gyro {
#if GYRO_OS_WINDOWS
    using OSPoll = void *; ///< HANDLE of the completion port.
    using OSSocket = gyro_socket_t;

    /// CreateIoCompletionPort() reports failure with NULL, not INVALID_HANDLE_VALUE.
    inline constexpr OSPoll kInvalidPoll = nullptr;

    inline constexpr OSSocket kInvalidSocket = GYRO_INVALID_SOCKET;
#else
    using OSPoll = int; ///< Descriptor of the kqueue or epoll instance.
    using OSSocket = gyro_socket_t;

    inline constexpr OSPoll kInvalidPoll = -1;
    inline constexpr OSSocket kInvalidSocket = GYRO_INVALID_SOCKET;
#endif
} // namespace gyro

#endif // !GYRO_PLATFORM_OSTYPES_H_
