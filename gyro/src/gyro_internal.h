// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_GYRO_INTERNAL_H_
#define GYRO_GYRO_INTERNAL_H_

#include <gyro/allocator.h>

#include "support/reqstore.h"

#include "handle_internal.h"
#include "request_minheap.h"

using PollHandler = uintptr_t;

struct Gyro {
    const gyro_allocator_t allocator;

    gyro::ReqHeap r_mheap;

    gyro::support::RequestStore requests;

    PollHandler handler = UINTMAX_MAX;

    bool should_terminate = false;

    explicit Gyro(const gyro_allocator_t *allocator) : allocator(*allocator), requests(&this->allocator) {
    }
};

namespace gyro {
    bool IOInit(Gyro *loop);

    void IOCleanup(const Gyro *loop);

    void IOPoll(const Gyro *loop, long long timeout);

    void ProcessHandle(Gyro *loop,  GyroHandle *handle, HandleDirection direction);
}

#endif // !GYRO_GYRO_INTERNAL_H_
