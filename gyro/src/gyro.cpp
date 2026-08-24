// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <cstring>

#include <gyro/gyro.h>

struct Gyro {
    gyro_allocator_t allocator;
};

extern "C" {
gyro_t *gyro_new(const gyro_allocator_t *allocator) {
    if (allocator == nullptr)
        allocator = gyro_default_allocator();

    auto *gyro = (Gyro *) allocator->alloc(sizeof(Gyro), allocator->ctx);
    if (gyro != nullptr) {
        memset(gyro, 0, sizeof(Gyro));

        gyro->allocator = *allocator;
    }

    return gyro;
}

void gyro_free(gyro_t *gyro) {
    if (gyro == nullptr)
        return;

    gyro->allocator.free(gyro, gyro->allocator.ctx);
}

const char *gyro_version(void) {
    return GYRO_VERSION_STRING;
}
} // extern "C"
