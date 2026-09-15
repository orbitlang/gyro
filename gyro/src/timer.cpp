// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <gyro/error.h>
#include <gyro/timer.h>

#include "gyro_internal.h"
#include "request_internal.h"

using namespace gyro;

extern "C" {
int gyro_timer_start(gyro_t *gyro, const long long timeout, const gyro_rq_user_cb cb, void *data,
                     gyro_request_t *out_token) {
    RequestIndex index{};

    if (timeout < 0 || cb == nullptr)
        return GYRO_EINVAL;

    auto *req = gyro->requests.Acquire(index);
    if (req == nullptr)
        return GYRO_ENOMEM;

    req->loop = gyro;
    req->data = data;
    req->cb_user = cb;
    req->io.every = timeout;

    Submit(req, out_token, timeout);

    return GYRO_PENDING;
}

int gyro_timer_start_periodic(gyro_t *gyro, const long long first, const long long period, const gyro_rq_user_cb cb,
                              void *data, gyro_request_t *out_token) {
    RequestIndex index{};

    if (period <= 0 || first < 0 || cb == nullptr)
        return GYRO_EINVAL;

    auto *req = gyro->requests.Acquire(index);
    if (req == nullptr)
        return GYRO_ENOMEM;

    req->loop = gyro;
    req->data = data;
    req->cb_user = cb;
    req->io.every = period;

    Submit(req, out_token, first);

    return GYRO_PENDING;
}
} // extern "C"
