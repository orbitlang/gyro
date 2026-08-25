// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <gyro/gyro.h>

#include "support/reqstore.h"

struct Gyro {
    const gyro_allocator_t allocator;

    gyro::support::RequestStore requests;

    explicit Gyro(const gyro_allocator_t *allocator) : allocator(*allocator), requests(&this->allocator) {
    }
};

extern "C" {
gyro_t *gyro_new(const gyro_allocator_t *allocator) {
    if (allocator == nullptr)
        allocator = gyro_default_allocator();

    auto *gyro = (Gyro *) allocator->alloc(sizeof(Gyro), allocator->ctx);
    if (gyro != nullptr) {
        new(gyro) Gyro(allocator);
    }

    return gyro;
}

void gyro_free(gyro_t *gyro) {
    if (gyro == nullptr)
        return;

    const auto allocator = gyro->allocator;

    gyro->~Gyro();

    allocator.free(gyro, allocator.ctx);
}

const char *gyro_version(void) {
    return GYRO_VERSION_STRING;
}
} // extern "C"
