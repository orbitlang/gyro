// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <chrono>

#include <gyro/version.h>

#include "gyro_internal.h"

using namespace gyro;

// TODO: temporary, remove once the backend can block
constexpr unsigned int kLoopTimeoutMs = 24;

static long long TimeNow() {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

static Request *RunTimer(Gyro *loop, const long long loop_time) {
    auto *request = loop->r_mheap.PeekMin();

    while (request != nullptr && request->timer.timeout <= loop_time) {
        // TODO: run actions

        loop->r_mheap.PopMin();
        loop->requests.Release(request);

        request = loop->r_mheap.PeekMin();
    }

    return request;
}

static void Loop(Gyro *loop) {
    while (!loop->should_terminate) {
        const auto loop_time = TimeNow();
        auto timeout = (long long) kLoopTimeoutMs;

        const auto *request = RunTimer(loop, loop_time);
        if (request != nullptr) {
            timeout = request->timer.timeout - loop_time;
            if (timeout < 0)
                timeout = 0;
        }

        IOPoll(loop, timeout);
    }
}

// PUBLIC

extern "C" {
gyro_t *gyro_new(const gyro_allocator_t *allocator) {
    if (allocator == nullptr)
        allocator = gyro_default_allocator();

    auto *gyro = (Gyro *) allocator->alloc(sizeof(Gyro), allocator->ctx);
    if (gyro != nullptr) {
        new(gyro) Gyro(allocator);

        if (!IOInit(gyro)) {
            gyro_free(gyro);

            return nullptr;
        }
    }

    return gyro;
}

void gyro_free(gyro_t *gyro) {
    if (gyro == nullptr)
        return;

    const auto allocator = gyro->allocator;

    if (gyro->handler != UINTMAX_MAX)
        IOCleanup(gyro);

    gyro->~Gyro();

    allocator.free(gyro, allocator.ctx);
}

const char *gyro_version(void) {
    return GYRO_VERSION_STRING;
}
} // extern "C"
