// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_SUPPORT_MINHEAP_H_
#define GYRO_SUPPORT_MINHEAP_H_

#include <cassert>
#include <type_traits>
#include <utility>

namespace gyro::support {
    /**
     * @brief Node comparison function.
     *
     * Must be a strict weak ordering: it returns true only when @p left sorts
     * strictly before @p right, and false for equal nodes.
     *
     * @tparam T Node type.
     */
    template<typename T>
    using heap_less = bool (*)(const T *left, const T *right);

    namespace minheap::check {
        /**
         * @brief Detects whether T carries the intrusive links MinHeap needs.
         *
         * Primary template: matches every type whose 'heap' member is missing or
         * incomplete, and therefore reports false.
         */
        template<typename T, typename = void>
        struct has_heap_node : std::false_type {
        };

        /**
         * @brief Partial specialization for types exposing heap.parent, heap.left and heap.right.
         *
         * The specialization is only viable when the three members exist; its value
         * then tells whether all of them are declared as T *.
         */
        template<typename T>
        struct has_heap_node<T, std::void_t<
                    decltype(std::declval<T &>().heap.parent),
                    decltype(std::declval<T &>().heap.left),
                    decltype(std::declval<T &>().heap.right)> >
                : std::bool_constant<
                    std::is_same_v<decltype(std::declval<T &>().heap.parent), T *> &&
                    std::is_same_v<decltype(std::declval<T &>().heap.left), T *> &&
                    std::is_same_v<decltype(std::declval<T &>().heap.right), T *>> {
        };

        template<typename T>
        inline constexpr bool has_heap_node_v = has_heap_node<T>::value;
    } // namespace minheap::check

    /**
     * @brief Min Heap.
     *
     * Intrusive and allocation free: the tree lives in the links carried by the
     * nodes themselves, so the heap never owns nor frees anything. Nodes must
     * outlive their stay in the heap.
     *
     * The tree is kept complete at all times, which keeps every operation
     * logarithmic and the shape independent of the insertion order.
     *
     * @tparam T Any object that exposes a nested struct named heap, containing the following 3 properties:
     * T * parent, T * left, T * right.
     * @tparam LESS The node comparison function.
     */
    template<typename T, heap_less<T> LESS>
    class MinHeap {
        static_assert(minheap::check::has_heap_node_v<T>,
                      "MinHeap<T> requires T to expose a nested member named 'heap' "
                      "holding three pointers: T *parent, T *left, T *right");

        static_assert(LESS != nullptr, "MinHeap<T, LESS> requires a non-null comparison function");

        /// Root of the tree, and therefore the lowest node. Null when the heap is empty.
        T *head = nullptr;

        /// Number of nodes currently in the heap.
        unsigned int count = 0;

        /**
         * @brief Returns the link that currently points to a node.
         *
         * That is the parent's left or right slot, or the heap root when the node
         * has no parent. Writing through the returned pointer re-attaches whatever
         * sits in that position.
         *
         * The parent's own links must still be up to date: this function tells the
         * two sides apart by comparing them against @p t.
         *
         * @param t Node to look up.
         * @return Address of the link pointing to @p t.
         */
        T **GetLink(T *t) {
            T *parent = t->heap.parent;

            if (parent == nullptr)
                return &this->head;

            if (parent->heap.left == t)
                return &parent->heap.left;

            return &parent->heap.right;
        }

        /**
         * @brief Points the children of a node back to it.
         *
         * Used after moving a node, to repair the upward half of the links its
         * children still hold.
         *
         * @param t Node whose children must be adopted.
         */
        static void SetChildrenParent(T *t) {
            if (t->heap.left != nullptr)
                t->heap.left->heap.parent = t;

            if (t->heap.right != nullptr)
                t->heap.right->heap.parent = t;
        }

        /**
         * @brief Check that the node is less than its parent, if not, fix the heap.
         *
         * Sifts the node up until its parent sorts before it or the root is reached.
         *
         * @param t Node to check.
         */
        void HeapVerify(T *t) {
            if (t->heap.parent == nullptr || LESS(t->heap.parent, t))
                return;

            do this->SwapNode(t); while (t->heap.parent != nullptr && LESS(t, t->heap.parent));
        }

        /**
         * @brief Swap node with its parent.
         *
         * Exchanges the two positions in the tree, rewiring the grandparent, both
         * sets of children and every parent pointer involved, so that @p t ends up
         * where its parent used to be.
         *
         * @param t Node to swap. Must have a parent.
        */
        void SwapNode(T *t) {
            //        +------------+       +------------+       +------------+
            // ...--->|Super Parent+------>|   Parent   +------>|   Child    |
            //        +------------+       +------------+       +------------+

            T *left = t->heap.left;
            T *right = t->heap.right;
            T **super_link = this->GetLink(t->heap.parent);

            // Move the parent node to the correct side of the child node 't'
            if (t->heap.parent->heap.left == t) {
                t->heap.left = t->heap.parent;
                t->heap.right = t->heap.parent->heap.right;
            } else {
                t->heap.right = t->heap.parent;
                t->heap.left = t->heap.parent->heap.left;
            }

            // Set parent left/right node
            t->heap.parent->heap.left = left;
            t->heap.parent->heap.right = right;
            SetChildrenParent(t->heap.parent);

            // Set new t node parent
            t->heap.parent = t->heap.parent->heap.parent;

            // Set the parent of the left and right nodes to t
            SetChildrenParent(t);

            // Set super parent link to t
            *super_link = t;
        }

    public:
        /**
         * @brief Returns the lesser node.
         *
         * The node stays in the heap. O(1).
         *
         * @return The lesser node.
         */
        T *PeekMin() const {
            return this->head;
        }

        /**
         * @brief Removes and returns the lesser node.
         *
         * O(log n).
         *
         * @return The lesser node.
         */
        T *PopMin() {
            return this->Remove(this->head);
        }

        /**
         * @brief Remove the node from the min heap.
         *
         * The last node of the tree takes the place of @p t and is then sifted in
         * whichever direction restores the ordering. The links of the removed node
         * are cleared, so it can be handed straight back to a free list or
         * re-inserted. O(log n).
         *
         * @param t Node to removed. Must belong to this heap.
         * @return The removed node.
         */
        T *Remove(T *t) {
            T **target_parent_link = &this->head;
            T *target = *target_parent_link;
            unsigned long path = 0;
            int node = 0;

            if (this->count == 0)
                return nullptr;

            if (this->count == 1) {
                this->head = nullptr;
                this->count -= 1;;

                // Cleans the removed item
                t->heap.parent = nullptr;
                t->heap.left = nullptr;
                t->heap.right = nullptr;
                return t;
            }

            // Walk down to the last node of the tree. Read as a 1-based index, its
            // position spells out the route: dropping the leading bit, every
            // remaining bit is one step, 0 left and 1 right. The loop collects
            // those bits least significant first, so the walk below consumes them
            // in the right order.
            for (auto i = this->count; i >= 2; i /= 2) {
                path = (path << 1) | (i & 1);
                node++;
            }

            while (node-- > 0) {
                if (path & 1) {
                    target_parent_link = &target->heap.right;
                    target = target->heap.right;
                } else {
                    target_parent_link = &target->heap.left;
                    target = target->heap.left;
                }

                path >>= 1;
            }

            // Detach the target from its current position (last node in the heap)
            *target_parent_link = nullptr;

            // The node being removed is itself the last one: it has just been
            // detached and there is nothing left to move into its place.
            if (target == t) {
                // Cleans the removed item
                t->heap.parent = nullptr;
                t->heap.left = nullptr;
                t->heap.right = nullptr;

                this->count -= 1;;

                return t;
            }

            // Target is the last node inserted in the heap
            assert(target->heap.left == nullptr);
            assert(target->heap.right == nullptr);

            // Move target node in place of the removed node
            *GetLink(t) = target;
            target->heap.parent = t->heap.parent;

            // Set left and right nodes to target
            target->heap.left = t->heap.left != target ? t->heap.left : nullptr;
            target->heap.right = t->heap.right != target ? t->heap.right : nullptr;
            SetChildrenParent(target);

            // Cleans the removed item
            t->heap.parent = nullptr;
            t->heap.left = nullptr;
            t->heap.right = nullptr;

            this->HeapVerify(target);

            this->count -= 1;;

            // Down check
            while (target->heap.left != nullptr) {
                T *to_check = target->heap.left;

                if (target->heap.right != nullptr && LESS(target->heap.right, target->heap.left))
                    to_check = target->heap.right;

                if (!LESS(to_check, target))
                    break;

                this->SwapNode(to_check);
            }

            return t;
        }

        /**
         * @brief Places the node into the min heap.
         *
         * The node is appended in the only position that keeps the tree complete,
         * then sifted up to its place. Its three links are overwritten, so the node
         * must not already belong to a heap. O(log n).
         *
         * @param t Node to place.
         */
        void Insert(T *t) {
            T **current = &this->head;
            T *parent = *current;
            unsigned long path = 0;
            int node = 0;

            // Route to the first free position, the one numbered count + 1.
            // Same bit walk as in Remove().
            for (auto i = this->count + 1; i >= 2; i /= 2) {
                path = (path << 1) | (i & 1);
                node++;
            }

            while (node-- > 0) {
                parent = *current;

                if (path & 1)
                    current = &((*current)->heap.right);
                else
                    current = &((*current)->heap.left);

                path >>= 1;
            }

            t->heap.parent = parent;
            *current = t;

            this->count += 1;;

            this->HeapVerify(t);
        }
    };
} // namespace gyro::support

#endif // !GYRO_SUPPORT_MINHEAP_H_
