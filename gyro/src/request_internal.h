// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_REQUEST_INTERNAL_H_
#define GYRO_REQUEST_INTERNAL_H_

#include <gyro/request.h>

namespace gyro {
    struct Request {
        uint64_t generation;
        uint32_t index;
        uint32_t next_free;

        struct {
            Request *parent;

            Request *left;
            Request *right;
        } heap;

        struct {
            long long id;
            long long timeout;
        } timer;
    };

    struct RequestIndex {
        struct Fields {
            uint64_t generation: 40;
            uint64_t index: 24;
        };

        union {
            Fields fields;

            uint64_t _opaque;
        };
    };
}

#endif // !GYRO_REQUEST_INTERNAL_H_
