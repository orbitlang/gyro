// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_PLATFORM_POSIX_NET_SOCKET_H_
#define GYRO_PLATFORM_POSIX_NET_SOCKET_H_

#include <netinet/in.h>
#include <sys/socket.h>

#include <gyro/net.h>
#include <gyro/platform.h>

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
#if (GYRO_OS_LINUX || defined(__FreeBSD__)) && defined(SOCK_CLOEXEC) && defined(SOCK_NONBLOCK)
#define GYRO_HAS_ACCEPT4 1
#endif

namespace gyro {
    /**
     * @brief Every flag a bind understands.
     *
     * A bit outside it is refused rather than ignored: a flag from a newer
     * header, or a typo, would otherwise bind without the option the caller
     * asked for and leave nothing to say so.
     */
    constexpr unsigned int kBindFlags = GYRO_BIND_REUSEADDR;

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
     * @brief Sets on the descriptor the options the bind flags ask for.
     *
     * The same call for every protocol, because here each flag is one socket
     * option and what it then does is the kernel's business, protocol by
     * protocol. Windows is where that stops being true, SO_REUSEADDR there
     * lets a socket take a port somebody else is using, so its backend is
     * where the protocol has to be asked.
     *
     * @param socket An open descriptor, not yet bound.
     * @param flags Bits of kBindFlags, already validated by the caller, which
     *            has to refuse the others before opening anything.
     * @return GYRO_COMPLETED, or a negative status.
     */
    int ApplyBindFlags(OSSocket socket, unsigned int flags);

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

    /**
     * @brief Length of an IP address, read off its family.
     *
     * The public API takes addresses without a length, and this is why it can:
     * the two families gyro accepts each have one fixed size, so the family
     * already says how many bytes there are.
     *
     * @return The length, or 0 for a family gyro does not accept.
     */
    inline socklen_t AddrLen(const sockaddr *addr) {
        switch (addr->sa_family) {
            case AF_INET:
                return sizeof(sockaddr_in);
            case AF_INET6:
                return sizeof(sockaddr_in6);
            default:
                return 0;
        }
    }
} // namespace gyro

#endif // !GYRO_PLATFORM_POSIX_NET_SOCKET_H_
