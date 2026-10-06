// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_PLATFORM_POSIX_NET_SOCKET_H_
#define GYRO_PLATFORM_POSIX_NET_SOCKET_H_

#include <sys/socket.h>

#include "handle_internal.h"

/**
 * @brief Whether accept4() can be used in place of accept().
 *
 * It is not POSIX and has no feature macro of its own, so the platforms known
 * to have it are named. Linux declares it only under _GNU_SOURCE, which the
 * C++ standard library already defines. NetBSD and OpenBSD have it too and
 * could be added; being wrong there is a build failure rather than a silent
 * fallback, so they are left out until somebody builds on them.
 */
#if (defined(__linux__) || defined(__FreeBSD__)) && defined(SOCK_CLOEXEC) && defined(SOCK_NONBLOCK)
#define GYRO_HAS_ACCEPT4 1
#endif

namespace gyro {
    /**
     * @brief Flags to ask socket() and accept4() for at creation time.
     *
     * What the kernel can do while making the descriptor, ConfigureSocket()
     * does not have to do afterwards: it spares two round trips each, and
     * leaves no window in which the descriptor exists without FD_CLOEXEC for a
     * concurrent fork to inherit. Zero where the platform has no such flags,
     * macOS among them, and then the fcntl path is the only way.
     */
#if defined(SOCK_CLOEXEC) && defined(SOCK_NONBLOCK)
    constexpr int kSockCreateFlags = SOCK_CLOEXEC | SOCK_NONBLOCK;
#else
    constexpr int kSockCreateFlags = 0;
#endif

    /// True when socket() hands back a descriptor that needs no fixing up.
    constexpr bool kDescriptorReady = kSockCreateFlags != 0;

    /// The same question for accept(), and deliberately not the same answer:
    /// a platform can have the creation flags and still lack accept4(), and
    /// then what it returns is a plain blocking descriptor.
#if defined(GYRO_HAS_ACCEPT4)
    constexpr bool kAcceptedDescriptorReady = kSockCreateFlags != 0;
#else
    constexpr bool kAcceptedDescriptorReady = false;
#endif

    /**
     * @brief Puts a descriptor into the state the loop needs it to be in.
     *
     * Non-blocking above all: the whole model rests on a syscall that returns
     * rather than waits, and a descriptor without O_NONBLOCK stalls the loop on
     * the first operation that cannot be served at once. Close-on-exec keeps a
     * child process from inheriting it, and SO_NOSIGPIPE, where it exists,
     * turns a write to a closed peer into an error instead of a SIGPIPE that
     * would end the process. MSG_NOSIGNAL does the same thing per send call
     * rather than per socket; the two are independent, and each is used
     * wherever the platform has it.
     *
     * Applies to descriptors gyro did not open either: one that arrives from
     * accept() goes through here too.
     *
     * @param socket An open descriptor. It stays the caller's: a failure here
     *             leaves it open, and closing it is the caller's to do, since
     *             only the caller knows whether it was theirs to begin with.
     * @param descriptor_ready True when the descriptor was created with
     *                       kSockCreateFlags and the non-blocking and
     *                       close-on-exec part is therefore already done. The
     *                       socket options are set either way, which is why
     *                       this takes a flag rather than being skipped: one
     *                       place decides what a gyro descriptor needs, and a
     *                       caller only says what the kernel already did.
     * @return GYRO_COMPLETED, or a negative status.
     */
    int ConfigureSocket(OSSocket socket, bool descriptor_ready);

    /**
     * @brief Gives the handle a descriptor, if it does not have one already.
     *
     * Idempotent, and that is what the public API rests on: bind and connect
     * call this without knowing whether the caller opened the socket first to
     * configure it, and a handle that came from accept() keeps the descriptor
     * it arrived with.
     *
     * @param handle Where the descriptor lands, and what is read to find out
     *             whether one is needed at all. From the moment it is written
     *             the descriptor belongs to the loop, which closes it when the
     *             handle is buried.
     * @param family Address family, as passed to socket().
     * @param type SOCK_STREAM, SOCK_DGRAM, whichever the protocol wants.
     * @return GYRO_COMPLETED, or a negative status. A failure leaves the handle
     *         without a descriptor rather than with a half-configured one.
     */
    int OpenSocket(GyroHandle *handle, int family, int type);
} // namespace gyro

#endif // !GYRO_PLATFORM_POSIX_NET_SOCKET_H_
