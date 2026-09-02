// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <gyro/tcp.h>

#include "gyro_internal.h"
#include "handle_internal.h"

struct GyroTcp {
    GyroHandle handle;
};

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

extern "C" {
gyro_tcp_t *gyro_tcp_new(gyro_t *gyro) {
    if (gyro == nullptr)
        return nullptr;

    const auto *allocator = &gyro->allocator;

    auto *tcp = (GyroTcp *) allocator->alloc(sizeof(GyroTcp), allocator->ctx);
    if (tcp != nullptr) {
        new(tcp) GyroTcp();

        tcp->handle.gyro = gyro;
    }

    return tcp;
}
} // extern "C"
