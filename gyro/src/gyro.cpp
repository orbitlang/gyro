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

static GyroRequest *RunTimer(Gyro *loop, const long long loop_time) {
    for (;;) {
        auto *request = loop->r_mheap.PeekMin();
        if (request == nullptr || request->timer.timeout > loop_time)
            return request;

        loop->r_mheap.PopMin();

        bool remove = true;
        if (request->timer.cancel_on_timeout) {
            // An I/O deadline: the operation is marked canceled and left where it
            // is. ProcessHandle owns its removal, so nothing is released here.
            RequestIndex index{};

            index.fields.generation = request->generation;
            index.fields.index = request->index;

            gyro_request_t token;
            token._opaque = index._opaque;

            gyro_request_cancel(loop, token);

            remove = false;
        }

        if (request->cb_op != nullptr)
            request->cb_op(nullptr, request);

        if (request->cb_user != nullptr)
            request->cb_user(nullptr, 0, 0, request->data);

        if (remove)
            FinishRequest(loop, request);
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

bool gyro::ProcessHandle(Gyro *loop, GyroHandle *handle, const HandleDirection direction) {
    auto *queue = &handle->in;
    if (direction == HandleDirection::OUT)
        queue = &handle->out;

    do {
        auto *request = queue->GetHead();
        if (request == nullptr)
            break;

        if (request->cancelled) {
            queue->Dequeue();

            FinishRequest(loop, request);

            continue;
        }

        auto status = GYRO_CB_SUCCESS;

        if (request->cb_op != nullptr) {
            status = request->cb_op(handle, request);
            if (status == GYRO_CB_RETRY)
                return true;
        }

        if (request->cb_user != nullptr)
            request->cb_user(handle, status, request->io.transferred, request->data);

        queue->Dequeue();

        FinishRequest(loop, request);
    } while (queue->GetHead() != nullptr);

    return false;
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
