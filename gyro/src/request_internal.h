// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_REQUEST_INTERNAL_H_
#define GYRO_REQUEST_INTERNAL_H_

#include <gyro/buf.h>
#include <gyro/request.h>

namespace gyro {
    enum class RequestKind : uint16_t {
        OP = 0, // Default, normal operation
        CANCEL,
        CLOSE
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
        union {
            GyroHandle *peer;

            gyro_buf_t *buf;

            /// Target request to operate
            gyro::RequestIndex target;

            long long every;
        };

        unsigned int nbufs;

        size_t offset;

        uint64_t transferred;
    } io;

    /// Completion status: GYRO_COMPLETED if still active, otherwise the reason
    /// the operation was terminated. Set when the decision is made and retained
    /// until reported, which may be delayed on completion port implementations.
    short abandoned;

    gyro::RequestKind kind;

    /// Which queue of that handle holds it.
    gyro_dir_t direction;
};

namespace gyro {
    int NewRequest(GyroHandle *handle, gyro_dir_t direction, gyro_rq_op_cb cb_op,
                   gyro_rq_user_cb cb_user, void *data, GyroRequest **out_request);

    void CancelRequest(GyroRequest *request);
} // namespace gyro

#endif // !GYRO_REQUEST_INTERNAL_H_
