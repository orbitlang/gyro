// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#else
#include <cerrno>
#endif

#include <gyro/error.h>

#include "error_internal.h"

#define GYRO_ERROR_MAP(XX)                                                  \
    XX(GYRO_COMPLETED, "operation completed")                               \
    XX(GYRO_PENDING, "operation pending")                                   \
    XX(GYRO_STOPPED, "loop stopped on request")                             \
    XX(GYRO_EUNKNOWN, "unknown error")                                      \
    XX(GYRO_EINVAL, "invalid argument")                                     \
    XX(GYRO_ENOMEM, "out of memory")                                        \
    XX(GYRO_ENOTSUP, "operation not supported")                             \
    XX(GYRO_ECANCELED, "operation canceled")                                \
    XX(GYRO_ETIMEDOUT, "operation timed out")                               \
    XX(GYRO_EBADF, "bad or closing handle")                                 \
    XX(GYRO_EOF, "end of file")                                             \
    XX(GYRO_ECONNRESET, "connection reset by peer")                         \
    XX(GYRO_ECONNREFUSED, "connection refused")                             \
    XX(GYRO_ECONNABORTED, "software caused connection abort")               \
    XX(GYRO_EPIPE, "broken pipe")                                           \
    XX(GYRO_EHOSTUNREACH, "host is unreachable")                            \
    XX(GYRO_ENETUNREACH, "network is unreachable")                          \
    XX(GYRO_EADDRINUSE, "address already in use")                           \
    XX(GYRO_EADDRNOTAVAIL, "address not available")                         \
    XX(GYRO_EMFILE, "too many open files")                                  \
    XX(GYRO_EACCES, "permission denied")

// Deliberately absent from both tables: EAGAIN/EWOULDBLOCK becomes
// GYRO_CB_RETRY and EINTR is retried inside the operation, so neither ever
// reaches a caller.
int gyro::ErrorToStatus(const int error) {
#if defined(_WIN32)
    switch (error) {
        case WSAEINVAL: return GYRO_EINVAL;
        case WSAENOBUFS: return GYRO_ENOMEM;
        case WSAEOPNOTSUPP:
        case WSAEAFNOSUPPORT:
        case WSAEPROTONOSUPPORT: return GYRO_ENOTSUP;

        // CancelIoEx and a closed handle both land here
        case WSA_OPERATION_ABORTED: return GYRO_ECANCELED;
        case WSAETIMEDOUT: return GYRO_ETIMEDOUT;
        case WSAEBADF:
        case WSAENOTSOCK: return GYRO_EBADF;

        // The overlapped equivalent of a connection reset.
        case ERROR_NETNAME_DELETED:
        case WSAECONNRESET: return GYRO_ECONNRESET;
        case WSAECONNREFUSED: return GYRO_ECONNREFUSED;
        case WSAECONNABORTED: return GYRO_ECONNABORTED;
        case WSAESHUTDOWN: return GYRO_EPIPE;
        case WSAEHOSTUNREACH:
        case WSAEHOSTDOWN: return GYRO_EHOSTUNREACH;
        case WSAENETUNREACH:
        case WSAENETDOWN:
        case WSAENETRESET: return GYRO_ENETUNREACH;

        case WSAEADDRINUSE: return GYRO_EADDRINUSE;
        case WSAEADDRNOTAVAIL: return GYRO_EADDRNOTAVAIL;
        case WSAEMFILE: return GYRO_EMFILE;
        case WSAEACCES: return GYRO_EACCES;

        default: return GYRO_EUNKNOWN;
    }
#else
    switch (error) {
        case EINVAL: return GYRO_EINVAL;
        case ENOMEM:
        case ENOBUFS: return GYRO_ENOMEM;
        case ENOTSUP:
#if defined(EOPNOTSUPP) && EOPNOTSUPP != ENOTSUP
        case EOPNOTSUPP:
#endif
        case EAFNOSUPPORT:
        case EPROTONOSUPPORT: return GYRO_ENOTSUP;

        case ECANCELED: return GYRO_ECANCELED;
        case ETIMEDOUT: return GYRO_ETIMEDOUT;
        case EBADF:
        case ENOTSOCK: return GYRO_EBADF;

        case ECONNRESET: return GYRO_ECONNRESET;
        case ECONNREFUSED: return GYRO_ECONNREFUSED;
        case ECONNABORTED: return GYRO_ECONNABORTED;
        case EPIPE:
#ifdef ESHUTDOWN
        case ESHUTDOWN:
#endif
            return GYRO_EPIPE;
        case EHOSTUNREACH:
#ifdef EHOSTDOWN
        case EHOSTDOWN:
#endif
            return GYRO_EHOSTUNREACH;
        case ENETUNREACH:
        case ENETDOWN:
        case ENETRESET: return GYRO_ENETUNREACH;

        case EADDRINUSE: return GYRO_EADDRINUSE;
        case EADDRNOTAVAIL: return GYRO_EADDRNOTAVAIL;
        case EMFILE:
        case ENFILE: return GYRO_EMFILE;
        case EACCES:
        case EPERM: return GYRO_EACCES;

        default: return GYRO_EUNKNOWN;
    }
#endif
}

extern "C" {
const char *gyro_strerror(int code) {
    switch (code) {
#define XX(name, description) case name: return description;
        GYRO_ERROR_MAP(XX)
#undef XX
        default:
            return "unknown error";
    }
}

const char *gyro_err_name(int code) {
    switch (code) {
#define XX(name, description) case name: return #name;
        GYRO_ERROR_MAP(XX)
#undef XX
        default:
            return "GYRO_EUNKNOWN";
    }
}
} // extern "C"
