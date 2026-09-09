// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <gyro/error.h>
#include <gyro/loop.h>
#include <gyro/request.h>

#include "gyro_internal.h"

using namespace gyro;

int gyro::NewRequest(GyroHandle *handle, const gyro_dir_t direction, const gyro_rq_op_cb cb_op,
                     const gyro_rq_user_cb cb_user, void *data, GyroRequest **out_request) {
    if (handle == nullptr || handle->gyro == nullptr)
        return GYRO_EINVAL;

    if (handle->state == HandleState::CLOSING)
        return GYRO_EBADF;

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
int gyro_request_cancel(const gyro_t *gyro, const gyro_request_t token) {
    RequestIndex tk{};

    tk._opaque = token._opaque;

    auto *request = gyro->requests.Resolve(tk);
    if (request != nullptr)
        CancelRequest(request);

    return GYRO_COMPLETED;
}

int gyro_request_submit(gyro_handle_t *handle, void *data, const gyro_rq_op_cb cb_op, const gyro_rq_user_cb cb_user,
                        gyro_request_t *out_token, const long long timeout, const gyro_dir_t direction) {
    GyroRequest *req;
    auto status = NewRequest(handle, direction, cb_op, cb_user, data, &req);
    if (status != GYRO_COMPLETED)
        return status;

    status = Submit(req, out_token, timeout);
    if (status != GYRO_COMPLETED)
        return status;

    return GYRO_PENDING;
}

void gyro_op_complete(gyro_op_t *op, const int status, const size_t transferred) {
    if (op->handle != nullptr) {
        auto *queue = QueueFor(op->handle, op->direction);
        queue->Remove(op);
    }

    if (op->cb_user != nullptr)
        op->cb_user(op->handle, status, transferred, op->data);

    FinishRequest(op->loop, op);
}
} // extern "C"
