// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <chrono>

#include <gyro/error.h>
#include <gyro/version.h>

#include "gyro_internal.h"

using namespace gyro;

/// Timeout meaning "no deadline of our own": block until the backend has
/// something to report.
constexpr long long kBlockForever = -1;

/// Whether the loop still owes somebody something.
static bool HasWork(const Gyro *loop) {
    return loop->request_count != 0
           || loop->closing_queue != nullptr
           || loop->mpsc_queue.load(std::memory_order_relaxed) != nullptr;
}

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
        // The claims, not the queues. A claim is held by everything that exists
        // for this handle anywhere: queued here, still on the wakeup queue, or
        // in the middle of an inline attempt on another thread. Zero claims
        // implies empty queues, and it is the only condition under which
        // freeing the handle cannot pull memory out from under something that
        // still has a pointer to it.
        if (PendingFor(*link, GYRO_DIR_IN).load(std::memory_order_acquire) != 0
            || PendingFor(*link, GYRO_DIR_OUT).load(std::memory_order_acquire) != 0) {
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

static void DrainMPSC(Gyro *loop) {
    loop->wakeup_pending.clear(std::memory_order_release);

    auto *queue = loop->mpsc_queue.exchange(nullptr, std::memory_order_acq_rel);

    GyroRequest *ordered = nullptr;

    while (queue != nullptr) {
        auto *next = queue->queue.next;

        queue->queue.next = ordered;
        ordered = queue;

        queue = next;
    }

    while (ordered != nullptr) {
        auto *next = ordered->queue.next;

        ordered->queue.next = nullptr;

        if (ordered->kind == RequestKind::CANCEL) {
            auto *target = loop->requests.Resolve(ordered->io.target);
            if (target != nullptr)
                CancelRequest(target);

            loop->requests.Release(ordered);

            ordered = next;

            continue;
        }

        if (ordered->kind == RequestKind::CLOSE) {
            auto *h = ordered->io.peer;

            for (auto *cursor = h->in.GetHead(); cursor != nullptr; cursor = cursor->queue.next)
                CancelRequest(cursor);

            for (auto *cursor = h->out.GetHead(); cursor != nullptr; cursor = cursor->queue.next)
                CancelRequest(cursor);

            loop->AddToClosingQueue(h);

            loop->requests.Release(ordered);

            ordered = next;

            continue;
        }

        const auto cb = ordered->cb_user;
        auto *handle = ordered->handle;
        auto *data = ordered->data;

        const auto status = Submit(ordered, nullptr, ordered->timer.timeout);
        if (status != GYRO_COMPLETED && cb != nullptr)
            cb(handle, status, 0, data);

        // Closed while it was on its way here. The close's own walk is behind
        // every submit pushed before it and finds those; this one was pushed
        // after, having read the handle as open a moment too early. It gets
        // what the walk gave the others, and nothing is left on a closing
        // handle with nobody to end it. Only when Submit succeeded: on failure
        // the request has already been finished and released.
        if (status == GYRO_COMPLETED && handle != nullptr && !IsActive(handle))
            CancelRequest(ordered);

        ordered = next;
    }
}

static int Loop(Gyro *loop) {
    loop->th_loop_id = std::this_thread::get_id();

    while (!loop->should_terminate.load(std::memory_order_relaxed)) {
        if (!HasWork(loop))
            return GYRO_COMPLETED;

        loop->time = TimeNow();
        auto timeout = kBlockForever;

        DrainMPSC(loop);

        const auto *request = RunTimer(loop, loop->time);
        if (request != nullptr) {
            timeout = request->timer.timeout - loop->time;
            if (timeout < 0)
                timeout = 0;
        } else if (!HasWork(loop))
            continue;

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
    auto *loop = request->loop;

    RequestIndex index{};
    index.fields.generation = request->generation;
    index.fields.index = request->index;

    if (out_token != nullptr)
        out_token->_opaque = index._opaque;

    if (loop->th_loop_id != std::this_thread::get_id()) {
        // Written before the request is published, not after: the exchange
        // below hands it to the loop, which reads the deadline out of it as
        // soon as it drains. Writing it afterwards is a race the loop loses
        // silently, by finding the zero the slot was blanked with and arming
        // no deadline at all.
        request->timer.timeout = timeout;

        PostToLoop(loop, request);

        return GYRO_PENDING;
    }

    loop->request_count += 1;

    if (request->handle == nullptr) {
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

    if (request->handle != nullptr)
        gyro_handle_try_end(request->handle, request->direction);
}

void gyro::PostToLoop(Gyro *loop, GyroRequest *request) {
    request->loop = loop;

    auto *last = loop->mpsc_queue.load(std::memory_order_relaxed);

    do
        request->queue.next = last;
    while (!loop->mpsc_queue.compare_exchange_strong(last,
                                                     request,
                                                     std::memory_order_release,
                                                     std::memory_order_relaxed));

    if (!loop->wakeup_pending.test_and_set(std::memory_order_acquire))
        IOWakeup(loop);
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
        gyro->th_loop_id = std::this_thread::get_id();
    }

    return gyro;
}

int gyro_free(gyro_t *gyro) {
    if (gyro == nullptr)
        return GYRO_COMPLETED;

    assert(gyro_on_loop_thread(gyro));

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

int gyro_on_loop_thread(const gyro_t *gyro) {
    if (gyro == nullptr)
        return 0;

    return gyro->th_loop_id.load(std::memory_order_relaxed) == std::this_thread::get_id();
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
