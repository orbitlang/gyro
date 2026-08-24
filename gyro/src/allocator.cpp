// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <cstdlib>

#include <gyro/allocator.h>

extern "C" {
static void *DefaultAlloc(const size_t size, void *ctx) {
    (void) ctx;

    return std::malloc(size);
}

static void DefaultFree(void *ptr, void *ctx) {
    (void) ctx;

    std::free(ptr);
}
} // extern "C"

static constexpr gyro_allocator_t kDefaultAllocator = {DefaultAlloc, DefaultFree, nullptr};

extern "C" const gyro_allocator_t *gyro_default_allocator(void) {
    return &kDefaultAllocator;
}
