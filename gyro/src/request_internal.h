// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_REQUEST_INTERNAL_H_
#define GYRO_REQUEST_INTERNAL_H_

#include <gyro/buf.h>
#include <gyro/request.h>

#include "hdirection.h"

struct GyroRequest {
    gyro_t *loop;

    /// Handle the operation runs on, null for a plain timer.
    GyroHandle *handle;

    uint64_t generation;
    uint32_t index;
    uint32_t next_free;

    gyro_rq_op_cb cb_op;
    gyro_rq_user_cb cb_user;

    void *data;

    struct {
        GyroRequest *parent;

        GyroRequest *left;
        GyroRequest *right;
    } heap;

    struct {
        GyroRequest *next;
        GyroRequest *prev;
    } queue;

    struct {
        long long id;
        long long timeout;

        bool cancel_on_timeout;
    } timer;

    struct {
        gyro_buf_t *buf;
        unsigned int nbufs;

        uint64_t transferred;
    } io;

    bool cancelled;

    /// Which queue of that handle holds it.
    gyro::HandleDirection direction;
};

namespace gyro {
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

    void CancelRequest(GyroRequest *request);
} // namespace gyro

#endif // !GYRO_REQUEST_INTERNAL_H_
