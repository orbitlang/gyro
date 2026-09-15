// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <gyro/error.h>
#include <gyro/loop.h>
#include <gyro/request.h>

#include "gyro_internal.h"

using namespace gyro;

int gyro::NewRequest(GyroHandle *handle, const gyro_dir_t direction, const gyro_rq_op_cb cb_op,
                     const gyro_rq_user_cb cb_user, void *data, GyroRequest **out_request) {
    if (handle == nullptr || handle->gyro == nullptr)
        return GYRO_EINVAL;

    if (!IsActive(handle))
        return GYRO_EBADF;

    // Every request on a handle carries the claim its submit took, and
    // FinishRequest() gives it back. A request born without one drives the
    // counter below zero, which is silent and permanent, so it is worth
    // catching here rather than in whichever operation stops going inline
    // hours later.
    assert(PendingFor(handle, direction).load(std::memory_order_relaxed) > 0
        && "a request must inherit a claim taken by gyro_handle_try_begin()");

    RequestIndex index{};

    auto *loop = handle->gyro;
    auto *req = loop->requests.Acquire(index);
    if (req == nullptr)
        return GYRO_ENOMEM;

    req->loop = loop;
    req->handle = handle;
    req->direction = direction;
    req->cb_op = cb_op;
    req->cb_user = cb_user;
    req->data = data;

    *out_request = req;

    return GYRO_COMPLETED;
}

void gyro::CancelRequest(GyroRequest *request) {
    if (request->abandoned)
        return;

    request->abandoned = GYRO_ECANCELED;

    if (request->handle != nullptr)
        request->timer.cancel_on_timeout = true;

    auto *loop = request->loop;
    if (loop->InHeap(request))
        loop->r_mheap.Remove(request);

    request->timer.timeout = loop->time;

    loop->r_mheap.Insert(request);
}

extern "C" {
int gyro_request_cancel(gyro_t *gyro, const gyro_request_t token) {
    RequestIndex tk{};

    if (!GYRO_REQUEST_IS_VALID(token))
        return GYRO_COMPLETED;

    auto *req = gyro->requests.Acquire(tk);
    if (req == nullptr)
        return GYRO_ENOMEM;

    req->io.target._opaque = token._opaque;
    req->kind = RequestKind::CANCEL;

    PostToLoop(gyro, req);

    return GYRO_COMPLETED;
}

int gyro_request_submit(gyro_handle_t *handle, void *data, const gyro_rq_op_cb cb_op, const gyro_rq_user_cb cb_user,
                        gyro_request_t *out_token, const long long timeout, const gyro_dir_t direction) {
    GyroRequest *req;
    auto status = NewRequest(handle, direction, cb_op, cb_user, data, &req);
    if (status != GYRO_COMPLETED) {
        gyro_handle_try_end(handle, direction);

        return status;
    }

    status = Submit(req, out_token, timeout);
    if (status != GYRO_COMPLETED)
        return status;

    return GYRO_PENDING;
}

void gyro_op_complete(gyro_op_t *op, const int status, const size_t transferred) {
    // Unlinking, dropping a timer and returning a slot to the store are three
    // pieces of the loop's own state. A driver that did its work elsewhere has
    // to come back here first.
    assert(gyro_on_loop_thread(op->loop));

    if (op->handle != nullptr) {
        auto *queue = QueueFor(op->handle, op->direction);
        queue->Remove(op);
    }

    auto *handle = op->handle;
    auto *cb_user = op->cb_user;
    auto *data = op->data;

    FinishRequest(op->loop, op);

    if (cb_user != nullptr)
        cb_user(handle, status, transferred, data);
}
} // extern "C"
