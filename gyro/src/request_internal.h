// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_REQUEST_INTERNAL_H_
#define GYRO_REQUEST_INTERNAL_H_

#include <gyro/request.h>

namespace gyro {
    struct Request {
    };

    struct RequestIndex {
        union {
            uint64_t _opaque;

            struct {
                uint64_t generation: 40;
                uint64_t index: 24;
            } fields;
        };
    };
}

#endif // !GYRO_REQUEST_INTERNAL_H_
