// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <chrono>

#include <gyro/error.h>
#include <gyro/version.h>

#include "gyro_internal.h"

using namespace gyro;

/// Timeout meaning "no deadline of our own": block until the backend has
/// something to report.
constexpr long long kBlockForever = -1;

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

static int Loop(Gyro *loop) {
    while (!loop->should_terminate.load(std::memory_order_relaxed)) {
        loop->time = TimeNow();
        auto timeout = kBlockForever;

        const auto *request = RunTimer(loop, loop->time);
        if (request != nullptr) {
            timeout = request->timer.timeout - loop->time;
            if (timeout < 0)
                timeout = 0;
        }

        const auto error = IOPoll(loop, timeout);
        if (error < 0)
            return error;
    }

    return GYRO_COMPLETED;
}

bool gyro::ProcessHandle(GyroHandle *handle, const gyro_dir_t direction) {
    const auto *queue = direction == GYRO_DIR_OUT ? &handle->out : &handle->in;

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

int gyro::Submit(GyroRequest *request, const long long timeout) {
    const auto timer_only = request->handle == nullptr;
    auto *loop = request->loop;

    if (timer_only) {
        request->timer.timeout = loop->time + timeout;
        request->timer.id = loop->time_id++;

        loop->r_mheap.Insert(request);

        return GYRO_COMPLETED;
    }

    auto *queue = QueueFor(request->handle, request->direction);

    const auto was_idle = queue->GetHead() == nullptr;

    if (timeout > 0) {
        request->timer.timeout = loop->time + timeout;
        request->timer.id = loop->time_id++;

        request->timer.cancel_on_timeout = true;

        loop->r_mheap.Insert(request);
    }

    queue->Enqueue(request);

    if (!was_idle)
        return GYRO_COMPLETED;

    const auto status = IOSubmit(request);
    if (status != GYRO_COMPLETED) {
        queue->Remove(request);

        FinishRequest(loop, request);
    }

    return status;
}

void gyro::FinishRequest(Gyro *loop, GyroRequest *request) {
    if (loop->InHeap(request))
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

        gyro->time = TimeNow();
    }

    return gyro;
}

int gyro_run(gyro_t *gyro) {
    if (gyro == nullptr)
        return GYRO_EINVAL;

    gyro->should_terminate.store(false, std::memory_order_relaxed);

    return Loop(gyro);
}

void gyro_stop(gyro_t *gyro) {
    if (gyro == nullptr)
        return;

    gyro->should_terminate.store(true, std::memory_order_relaxed);

    IOWakeup(gyro);
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
