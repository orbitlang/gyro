// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <chrono>

#include <gyro/error.h>
#include <gyro/version.h>

#include "gyro_internal.h"

using namespace gyro;

// TODO: temporary, remove once the backend can block
constexpr unsigned int kLoopTimeoutMs = 24;

static long long TimeNow() {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

static GyroRequest *RunTimer(Gyro *loop, const long long loop_time) {
    for (;;) {
        auto *request = loop->r_mheap.PeekMin();
        if (request == nullptr || request->timer.timeout > loop_time)
            return request;

        loop->r_mheap.PopMin();

        bool remove = true;
        if (request->timer.cancel_on_timeout) {
            request->cancelled = true;

            remove = IOCancel(request);
        }

        if (remove)
            gyro_op_complete(request, request->cancelled ? GYRO_ECANCELED : GYRO_COMPLETED, request->io.transferred);
    }
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

        const auto error = IOPoll(loop, timeout);
        if (error < 0) {
            // TODO: report error;
        }
    }
}

bool gyro::IOCancel(GyroRequest *request) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    // TODO: CancelIoEx, the completion packet reports the outcome.
    return false;
#else
    return true;
#endif
}

bool gyro::ProcessHandle(GyroHandle *handle, const HandleDirection direction) {
    const auto *queue = direction == HandleDirection::OUT ? &handle->out : &handle->in;

    for (;;) {
        auto *request = queue->GetHead();
        if (request == nullptr)
            return false;

        if (request->cancelled) {
            gyro_op_complete(request, GYRO_ECANCELED, request->io.transferred);

            continue;
        }

        // The operation reports its own outcome through gyro_op_complete, so
        // what comes back says only what the loop should do next.
        if (request->cb_op(handle, request) == GYRO_CB_RETRY)
            return true;
    }
}

void gyro::FinishRequest(Gyro *loop, GyroRequest *request) {
    const bool in_heap = request->heap.parent != nullptr
                         || request->heap.left != nullptr
                         || request->heap.right != nullptr
                         || loop->r_mheap.PeekMin() == request;

    if (in_heap)
        loop->r_mheap.Remove(request);

    loop->requests.Release(request);
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

    if (gyro->handler != kInvalidPoll)
        IOCleanup(gyro);

    gyro->~Gyro();

    allocator.free(gyro, allocator.ctx);
}

const char *gyro_version(void) {
    return GYRO_VERSION_STRING;
}
} // extern "C"
