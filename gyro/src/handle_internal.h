// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_HANDLE_INTERNAL_H_
#define GYRO_HANDLE_INTERNAL_H_

#include <atomic>

#include <gyro/handle.h>
#include <gyro/loop.h>

#include "support/queue.h"
#include "platform/ostypes.h"
#include "request_internal.h"

namespace gyro {
    enum class HandleState {
        ACTIVE,
        CLOSING
    };
} // namespace gyro

struct GyroHandle {
    gyro::support::Queue<GyroRequest> in;
    gyro::support::Queue<GyroRequest> out;

    std::atomic_uint32_t in_pending = 0;
    std::atomic_uint32_t out_pending = 0;

    GyroHandle *next = nullptr;

    gyro_t *gyro = nullptr;

    gyro_close_cb cb_close = nullptr;

    void *data = nullptr;

    gyro::OSSocket handle = gyro::kInvalidSocket;

    std::atomic<gyro::HandleState> state = gyro::HandleState::ACTIVE;
};

namespace gyro {
    /// True while the handle has not been handed to the loop for burial.
    inline bool IsActive(const GyroHandle *handle) {
        return handle->state.load(std::memory_order_acquire) == HandleState::ACTIVE;
    }

    template<typename T>
    std::conditional_t<
        std::is_const_v<std::remove_pointer_t<std::remove_reference_t<T> > >,
        const support::Queue<GyroRequest>,
        support::Queue<GyroRequest>
    >
    *QueueFor(T handle, const gyro_dir_t direction) {
        return direction == GYRO_DIR_OUT ? &handle->out : &handle->in;
    }

    /// The claim counter for one direction, which a submit takes and a request holds.
    template<typename T>
    std::conditional_t<
        std::is_const_v<std::remove_pointer_t<std::remove_reference_t<T> > >,
        const std::atomic_uint32_t,
        std::atomic_uint32_t
    >
    &PendingFor(T *handle, const gyro_dir_t direction) {
        return direction == GYRO_DIR_OUT ? handle->out_pending : handle->in_pending;
    }

    /**
     * @brief Holds the claim a submit takes on a direction, and gives it back.
     *
     * gyro_handle_try_begin() has to be balanced exactly once on every path
     * out of a submit, and a submit has more of those than are comfortable to
     * keep in one's head: a rejected argument, an attempt that succeeded
     * outright, a hard error from the syscall, a request that could not be
     * allocated. Missing one is silent, and costs that handle its fast path
     * for good, so the balancing is left to a destructor rather than to
     * whoever edits the function next.
     *
     * Transfer() is for the one path that must not give it back, where the
     * operation was queued and its request carries the claim until it reports.
     */
    class InlineGate {
        GyroHandle *handle_;

        gyro_dir_t direction_;

        bool may_try_;

    public:
        InlineGate(GyroHandle *handle, const gyro_dir_t direction)
            : handle_(handle),
              direction_(direction),
              may_try_(gyro_handle_try_begin(handle, direction) != 0) {
        }

        InlineGate(const InlineGate &) = delete;

        InlineGate &operator=(const InlineGate &) = delete;

        ~InlineGate() {
            if (this->handle_ != nullptr)
                gyro_handle_try_end(this->handle_, this->direction_);
        }

        /// Whether this caller owns the direction and may attempt the syscall.
        [[nodiscard]] bool MayTry() const { return this->may_try_; }

        /// Hands the claim to a request, which releases it when it reports.
        void Transfer() { this->handle_ = nullptr; }
    };

    void IOHandleClose(GyroHandle *handle);
} // namespace gyro

#endif // !GYRO_HANDLE_INTERNAL_H_
