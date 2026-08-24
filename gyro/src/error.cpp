// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <gyro/error.h>

#define GYRO_ERROR_MAP(XX)                                                  \
    XX(GYRO_COMPLETED, "operation completed")                               \
    XX(GYRO_PENDING, "operation pending")                                   \
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
