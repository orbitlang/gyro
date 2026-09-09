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
            // Already given up on means somebody asked for it, and that stays
            // the answer. Otherwise the deadline simply arrived, which is a
            // different thing to whoever is waiting: one says stop trying, the
            // other says it took too long.
            if (request->abandoned == GYRO_COMPLETED)
                request->abandoned = GYRO_ETIMEDOUT;

            remove = IOCancel(request);
        }

        if (remove)
            gyro_op_complete(request, request->abandoned, request->io.transferred);
    }
}

static void CloseHandles(Gyro *loop) {
    GyroHandle **link = &loop->closing_queue;

    while (*link != nullptr) {
        if ((*link)->in.Count() != 0 || (*link)->out.Count() != 0) {
            link = &(*link)->next;

            continue;
        }

        auto *handle = *link;

        *link = handle->next;

        IOHandleClose(handle);

        if (handle->cb_close != nullptr)
            handle->cb_close(handle);

        const auto allocator = loop->allocator;
        allocator.free(handle, allocator.ctx);
    }
}

static int Loop(Gyro *loop) {
    while (!loop->should_terminate.load(std::memory_order_relaxed)) {
        if (loop->request_count == 0 && loop->closing_queue == nullptr)
            return GYRO_COMPLETED;

        loop->time = TimeNow();
        auto timeout = kBlockForever;

        const auto *request = RunTimer(loop, loop->time);
        if (request != nullptr) {
            timeout = request->timer.timeout - loop->time;
            if (timeout < 0)
                timeout = 0;
        }

        if (loop->closing_queue != nullptr)
            timeout = 0;

        const auto error = IOPoll(loop, timeout);
        if (error < 0)
            return error;

        CloseHandles(loop);
    }

    return GYRO_STOPPED;
}

bool gyro::ProcessHandle(GyroHandle *handle, const gyro_dir_t direction) {
    const auto *queue = QueueFor(handle, direction);

    for (;;) {
        auto *request = queue->GetHead();
        if (request == nullptr)
            return false;

        // Given up on before it ever ran, or while a completion port still had
        // it. Either way the answer was decided elsewhere.
        if (request->abandoned) {
            gyro_op_complete(request, request->abandoned, request->io.transferred);

            continue;
        }

        // The operation reports its own outcome through gyro_op_complete, so
        // what comes back says only what the loop should do next.
        if (request->cb_op(handle, request) == GYRO_CB_RETRY)
            return true;
    }
}

int gyro::Submit(GyroRequest *request, gyro_request_t *out_token, const long long timeout) {
    const auto timer_only = request->handle == nullptr;
    auto *loop = request->loop;

    loop->request_count += 1;

    RequestIndex index{};
    index.fields.generation = request->generation;
    index.fields.index = request->index;

    if (out_token != nullptr)
        out_token->_opaque = index._opaque;

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

        if (out_token != nullptr)
            *out_token = gyro_request_invalid();
    }

    return status;
}

void gyro::FinishRequest(Gyro *loop, GyroRequest *request) {
    if (loop->InHeap(request))
        loop->r_mheap.Remove(request);

    loop->requests.Release(request);

    assert(loop->request_count > 0);

    loop->request_count -= 1;
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

int gyro_free(gyro_t *gyro) {
    if (gyro == nullptr)
        return GYRO_COMPLETED;

    // Releasing now would drop operations that still owe a callback, and
    // handles whose descriptors are still open. Refuse, and leave everything
    // as it was: winding down is the caller's to finish.
    if (gyro->request_count > 0 || gyro->closing_queue != nullptr)
        return GYRO_EBUSY;

    const auto allocator = gyro->allocator;

    if (gyro->handler != kInvalidPoll)
        IOCleanup(gyro);

    gyro->~Gyro();

    allocator.free(gyro, allocator.ctx);

    return GYRO_COMPLETED;
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

const char *gyro_version(void) {
    return GYRO_VERSION_STRING;
}
} // extern "C"
