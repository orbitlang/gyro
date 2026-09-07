// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_GYRO_INTERNAL_H_
#define GYRO_GYRO_INTERNAL_H_

#include <atomic>

#include <gyro/allocator.h>

#include "platform/backend.h"
#include "platform/ostypes.h"

#include "support/reqstore.h"

#include "handle_internal.h"
#include "request_minheap.h"

struct Gyro {
    gyro::BackendData backend{};

    const gyro_allocator_t allocator;

    gyro::support::RequestStore requests;

    gyro::ReqHeap r_mheap;

    GyroHandle *closing_queue = nullptr;

    /// Loop's notion of now, in milliseconds, refreshed once per iteration.
    /// Every deadline is computed against it rather than against a fresh
    /// reading, so requests submitted within one turn share a single now and
    /// expire in the turn their caller asked for.
    long long time = 0;

    /// Hands out the tie-break for timers sharing a deadline, so that equal
    /// deadlines still fire in submission order.
    long long time_id = 0;

    gyro::OSPoll handler = gyro::kInvalidPoll;

    std::atomic_bool should_terminate = false;

    explicit Gyro(const gyro_allocator_t *allocator) : allocator(*allocator), requests(&this->allocator) {
    }

    /**
     * @brief Tells whether a request is currently in the timer heap.
     *
     * Read off the request's own links rather than a flag, which works only
     * because every path that takes a node out of the heap clears them. The
     * root carries no links either, hence the last term.
     */
    bool InHeap(const GyroRequest *request) const {
        return request->heap.parent != nullptr
               || request->heap.left != nullptr
               || request->heap.right != nullptr
               || this->r_mheap.PeekMin() == request;
    }

    void AddToClosingQueue(GyroHandle *handle) {
        if (handle == nullptr)
            return;

        assert(handle->next == nullptr);
        assert(handle->gyro == this);

        handle->next = this->closing_queue;
        this->closing_queue = handle;
    }
};

namespace gyro {
    /**
     * @brief Backend hook: gives up on an operation the loop no longer wants.
     *
     * Called on the loop thread once the request is already marked cancelled.
     * Whether the request can be finished on the spot is the backend's call,
     * not the loop's: a reactor holds nothing of the caller's, so it says yes,
     * while a completion port still owns the buffer and has to start the
     * cancellation and wait for the packet that reports it.
     *
     * @return True when the caller must complete the request itself.
     */
    bool IOCancel(GyroRequest *request);

    /**
     * @brief Backend hook: opens the polling handle and whatever it needs.
     *
     * Leaves nothing behind on failure, so the loop can be freed straight away.
     */
    bool IOInit(Gyro *loop);

    /**
     * @brief Drains one direction of a handle, oldest request first.
     *
     * Stops at the first request that cannot make progress, which is what keeps
     * delivery in submission order. Each request reports itself through
     * gyro_op_complete(), so nothing here says how any of them ended.
     *
     * @return True when a request is still waiting and the handle has to be
     *         watched again.
     */
    bool ProcessHandle(GyroHandle *handle, gyro_dir_t direction);

    /**
     * @brief Backend hook: waits for events and dispatches them.
     *
     * @param loop The event loop instance to poll for events.
     * @param timeout Milliseconds to block for, or a negative value to block
     *                until something happens.
     */
    int IOPoll(Gyro *loop, long long timeout);

    /**
     * @brief Backend hook: starts watching for the request at the head.
     *
     * Only ever called for the request that has just become the head of its
     * direction: the ones behind it are covered by the same registration and
     * are reached by ProcessHandle() when it fires.
     */
    int IOSubmit(GyroRequest *request);

    /**
     * @brief Hands a prepared request over to the loop.
     *
     * Everything the operation needs is read from the request itself, so a
     * caller that filled it has nothing left to pass. A request carrying no
     * handle is a plain timer and only enters the heap; one carrying a handle
     * joins its direction's queue, and reaches the backend only if it lands at
     * the head.
     *
     * This is the slow path. The synchronous attempt happens before a request
     * exists completely, so an operation satisfied on the spot never gets
     * here.
     *
     * Nothing is left behind on failure: the request is unlinked, released, and
     * its user callback never runs, because the caller has yet to be handed a
     * token it could recognize the operation by.
     *
     * @param request The prepared request to submit to the loop.
     * @param out_token Output parameter that receives the token identifying this
     *                  operation, allowing the caller to reference or cancel it.
     * @param timeout Milliseconds from now, measured against the loop's notion
     *                of now rather than a fresh reading. On an operation it is
     *                a deadline, and anything not positive means it has none; on
     *                a plain timer it is the delay itself, and zero means the
     *                next turn.
     * @return GYRO_COMPLETED once the loop owns the request, or a negative
     *         status if the backend refused it.
     */
    int Submit(GyroRequest *request, gyro_request_t *out_token, long long timeout);

    /**
     * @brief Takes a finished request out of the loop and back into the store.
     *
     * Its deadline, if it had one and it never fired, is dropped here.
     */
    void FinishRequest(Gyro *loop, GyroRequest *request);

    /**
     * @brief Backend hook: releases what IOInit() opened.
     */
    void IOCleanup(const Gyro *loop);

    /**
     * @brief Backend hook: breaks the loop out of its wait.
     *
     * The one place the loop is reachable from another thread, and the reason
     * a loop blocking with no deadline can still be told anything at all.
     */
    void IOWakeup(const Gyro *loop);
}

#endif // !GYRO_GYRO_INTERNAL_H_
