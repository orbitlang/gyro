// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_SUPPORT_QUEUE_H_
#define GYRO_SUPPORT_QUEUE_H_

#include <cassert>
#include <type_traits>

namespace gyro::support {
    namespace queue::check {
        /**
         * @brief Detects whether T carries the intrusive links Queue needs.
         *
         * Primary template: matches every type whose 'queue' member is missing
         * or incomplete, and therefore reports false.
         */
        template<typename T, typename = void>
        struct has_queue_node : std::false_type {
        };

        /**
         * @brief Partial specialization for types exposing queue.next and queue.prev.
         *
         * The specialization is only viable when both members exist; its value
         * then tells whether both are declared as T *.
         */
        template<typename T>
        struct has_queue_node<T, std::void_t<
                    decltype(std::declval<T &>().queue.next),
                    decltype(std::declval<T &>().queue.prev)> >
                : std::bool_constant<
                    std::is_same_v<decltype(std::declval<T &>().queue.next), T *> &&
                    std::is_same_v<decltype(std::declval<T &>().queue.prev), T *>> {
        };

        template<typename T>
        inline constexpr bool has_queue_node_v = has_queue_node<T>::value;
    }

    /**
     * @brief Intrusive FIFO queue.
     *
     * Allocation free: the list lives in the links carried by the nodes
     * themselves, so the queue never owns nor frees anything. Nodes must
     * outlive their stay in it.
     *
     * Doubly linked so a node can be unlinked from the middle in constant time,
     * which is what cancelling a queued operation needs; enqueue and dequeue
     * alone would be served by a single link.
     *
     * Direction convention: **next runs from head to tail, prev from tail to
     * head**, so walking from GetHead() through queue.next yields the nodes in
     * the order they were enqueued.
     *
     * Every operation is O(1).
     *
     * @tparam T Any object that exposes a nested struct named queue, containing
     * the following 2 properties: T * next, T * prev.
     */
    template<typename T>
    class Queue {
        static_assert(queue::check::has_queue_node_v<T>,
                      "Queue<T> requires T to expose a nested member named 'queue' "
                      "holding two pointers: T *next, T *prev");


        T *head_ = nullptr; ///< Oldest node, the next one to leave. Null when empty.
        T *tail_ = nullptr; ///< Newest node. Null when empty.

        unsigned int items = 0;

    public:
        /**
         * @brief Removes and returns the node at the head.
         *
         * The links of the returned node are cleared, so it can be handed
         * straight back to a pool or enqueued somewhere else.
         *
         * @return The oldest node, or nullptr when the queue is empty.
         */
        T *Dequeue() {
            if (this->head_ == nullptr)
                return nullptr;

            auto *t = this->head_;

            this->head_ = t->queue.next;

            if (this->head_ != nullptr)
                this->head_->queue.prev = nullptr;
            else
                this->tail_ = nullptr;

            this->items -= 1;

            t->queue.next = nullptr;
            t->queue.prev = nullptr;

            return t;
        }

        /**
         * @brief Returns the node at the head without removing it.
         *
         * Also, the starting point for walking the queue through queue.next.
         *
         * @return The oldest node, or nullptr when the queue is empty.
         */
        T *GetHead() const {
            return this->head_;
        }

        /**
         * @brief Returns how many nodes the queue holds.
         */
        [[nodiscard]] unsigned int Count() const {
            return this->items;
        }

        /**
         * @brief Appends a node to the tail.
         *
         * Both of its links are overwritten, so the node must not already
         * belong to a queue.
         *
         * @param t Node to append.
         */
        void Enqueue(T *t) {
            t->queue.next = nullptr;
            t->queue.prev = this->tail_;

            if (this->tail_ != nullptr)
                this->tail_->queue.next = t;
            else
                this->head_ = t;

            this->tail_ = t;

            this->items += 1;
        }

        /**
         * @brief Unlinks a node from anywhere in the queue.
         *
         * Head, tail, and sole node are all handled. As with Dequeue(), the
         * links of the removed node are cleared.
         *
         * Removing a node twice, or one that belongs to another queue, would
         * splice the wrong list and drive the count below zero; the assertion
         * catches the second case in debug builds.
         *
         * @param t Node to unlink. Must belong to this queue.
         */
        void Remove(T *t) {
            assert(this->items > 0);

            if (t->queue.next != nullptr)
                t->queue.next->queue.prev = t->queue.prev;

            if (t->queue.prev != nullptr)
                t->queue.prev->queue.next = t->queue.next;

            if (this->head_ == t)
                this->head_ = t->queue.next;

            if (this->tail_ == t)
                this->tail_ = t->queue.prev;

            this->items -= 1;

            t->queue.next = nullptr;
            t->queue.prev = nullptr;
        }
    };
} // namespace gyro::support

#endif // !GYRO_SUPPORT_QUEUE_H_
