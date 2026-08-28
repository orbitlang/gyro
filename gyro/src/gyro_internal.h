// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_GYRO_INTERNAL_H_
#define GYRO_GYRO_INTERNAL_H_

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

    gyro::OSPoll handler = gyro::kInvalidPoll;

    bool should_terminate = false;

    explicit Gyro(const gyro_allocator_t *allocator) : allocator(*allocator), requests(&this->allocator) {
    }
};

namespace gyro {
    bool IOInit(Gyro *loop);

    bool ProcessHandle(Gyro *loop, GyroHandle *handle, HandleDirection direction);

    int IOPoll(Gyro *loop, long long timeout);

    void FinishRequest(Gyro *loop, GyroRequest *request);

    void IOCleanup(const Gyro *loop);
}

#endif // !GYRO_GYRO_INTERNAL_H_
