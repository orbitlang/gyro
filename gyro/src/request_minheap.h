// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_REQUEST_MINHEAP_H_
#define GYRO_REQUEST_MINHEAP_H_

#include "support/minheap.h"
#include "request_internal.h"

namespace gyro {
    inline bool RequestTimeoutLess(const GyroRequest *left, const GyroRequest *right) {
        if (left->timer.timeout < right->timer.timeout)
            return true;

        if (left->timer.timeout == right->timer.timeout)
            return left->timer.id < right->timer.id;

        return false;
    }

    using ReqHeap = support::MinHeap<GyroRequest, RequestTimeoutLess>;
}

#endif // !GYRO_REQUEST_MINHEAP_H_
