// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <cstring>
#include <algorithm>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include <support/minheap.h>

namespace {
    struct Node {
        struct {
            Node *parent;
            Node *left;
            Node *right;
        } heap;

        int key;
    };

    bool NodeLess(const Node *left, const Node *right) {
        return left->key < right->key;
    }

    using TestHeap = gyro::support::MinHeap<Node, NodeLess>;

    void ResetNode(Node *node, const int key) {
        std::memset(&node->heap, 0, sizeof(node->heap));
        node->key = key;
    }

    /**
     * @brief Walks the tree and asserts every structural invariant a min heap must hold.
     *
     * Deliberately independent of MinHeap internals: it only follows parent/left/right.
     *
     * @param node Subtree root.
     * @param parent Expected parent of node.
     * @param index 1-based position of node in the implicit complete tree.
     * @param indices Collects the visited positions.
     */
    void Walk(const Node *node, const Node *parent, unsigned long index,
              std::vector<unsigned long> &indices, int depth) {
        if (node == nullptr)
            return;

        ASSERT_LT(depth, 48) << "tree is too deep, likely a cycle";
        ASSERT_LT(indices.size(), 1u << 20u) << "too many nodes, likely a cycle";

        EXPECT_EQ(parent, node->heap.parent) << "parent pointer does not match the tree shape";

        if (parent != nullptr)
            EXPECT_FALSE(NodeLess(node, parent)) << "min heap property violated";

        if (node->heap.right != nullptr)
            EXPECT_NE(nullptr, node->heap.left) << "right child without a left one";

        indices.push_back(index);

        Walk(node->heap.left, node, index * 2, indices, depth + 1);
        Walk(node->heap.right, node, index * 2 + 1, indices, depth + 1);
    }

    /**
     * @brief Verifies the whole heap and returns how many nodes it holds.
     */
    size_t Verify(TestHeap &heap) {
        std::vector<unsigned long> indices;

        Walk(heap.PeekMin(), nullptr, 1, indices, 0);

        std::sort(indices.begin(), indices.end());

        EXPECT_EQ(indices.end(), std::unique(indices.begin(), indices.end()))
                            << "the same position is reachable twice";

        if (!indices.empty())
            EXPECT_EQ(indices.size(), indices.back())
                                << "tree is not complete: highest position does not match the node count";

        return indices.size();
    }

    TEST(MinHeapTest, FreshHeapIsEmpty) {
        alignas(TestHeap) unsigned char storage[sizeof(TestHeap)];
        std::memset(storage, 0xAB, sizeof(storage));

        auto *heap = new(storage) TestHeap;

        EXPECT_EQ(nullptr, heap->PeekMin());
    }

    TEST(MinHeapTest, PopOnEmptyHeapReturnsNull) {
        TestHeap heap{};

        EXPECT_EQ(nullptr, heap.PopMin());
    }

    TEST(MinHeapTest, InsertSingleNode) {
        TestHeap heap{};
        Node node{};

        ResetNode(&node, 42);
        heap.Insert(&node);

        EXPECT_EQ(&node, heap.PeekMin());
        EXPECT_EQ(1u, Verify(heap));
    }

    TEST(MinHeapTest, PopMinEmptiesTheHeap) {
        TestHeap heap{};
        Node node{};

        ResetNode(&node, 7);
        heap.Insert(&node);

        EXPECT_EQ(&node, heap.PopMin());
        EXPECT_EQ(nullptr, heap.PeekMin());
        EXPECT_EQ(0u, Verify(heap));
    }

    TEST(MinHeapTest, RemovedNodeIsUnlinked) {
        TestHeap heap{};
        std::vector<Node> nodes(3);

        for (int i = 0; i < 3; i++) {
            ResetNode(&nodes[i], i);
            heap.Insert(&nodes[i]);
        }

        Node *removed = heap.PopMin();

        EXPECT_EQ(nullptr, removed->heap.parent);
        EXPECT_EQ(nullptr, removed->heap.left);
        EXPECT_EQ(nullptr, removed->heap.right);
    }

    TEST(MinHeapTest, InsertKeepsInvariants) {
        TestHeap heap{};
        std::mt19937 rng(20260818);
        std::vector<Node> nodes(512);

        for (size_t i = 0; i < nodes.size(); i++) {
            ResetNode(&nodes[i], static_cast<int>(rng() % 128));

            heap.Insert(&nodes[i]);

            ASSERT_EQ(i + 1, Verify(heap)) << "after inserting node " << i;
        }
    }

    TEST(MinHeapTest, PopMinYieldsSortedOrder) {
        TestHeap heap{};
        std::mt19937 rng(20260818);
        std::vector<Node> nodes(512);
        std::vector<int> expected;

        for (auto & node : nodes) {
            ResetNode(&node, static_cast<int>(rng() % 128));
            heap.Insert(&node);
            expected.push_back(node.key);
        }

        std::sort(expected.begin(), expected.end());

        for (size_t i = 0; i < expected.size(); i++) {
            Node *min = heap.PopMin();

            ASSERT_NE(nullptr, min) << "heap ran out of nodes at step " << i;
            EXPECT_EQ(expected[i], min->key) << "at step " << i;
            ASSERT_EQ(nodes.size() - i - 1, Verify(heap)) << "after popping node " << i;
        }

        EXPECT_EQ(nullptr, heap.PopMin());
    }

    TEST(MinHeapTest, PopMinHandlesDuplicateKeys) {
        TestHeap heap{};
        std::vector<Node> nodes(16);

        for (auto & node : nodes) {
            ResetNode(&node, 5);
            heap.Insert(&node);
        }

        for (size_t i = 0; i < nodes.size(); i++) {
            Node *min = heap.PopMin();

            ASSERT_NE(nullptr, min);
            EXPECT_EQ(5, min->key);
        }

        EXPECT_EQ(0u, Verify(heap));
    }

    TEST(MinHeapTest, RemoveOnlyNode) {
        TestHeap heap{};
        Node node{};

        ResetNode(&node, 1);
        heap.Insert(&node);

        EXPECT_EQ(&node, heap.Remove(&node));
        EXPECT_EQ(0u, Verify(heap));
    }

    /**
     * Removes every position of every heap size in turn: this is the operation a
     * timer heap performs whenever a timeout is cancelled.
     */
    TEST(MinHeapTest, RemoveAnyPosition) {
        for (size_t size = 2; size <= 16; size++) {
            for (size_t victim = 0; victim < size; victim++) {
                TestHeap heap{};
                std::vector<Node> nodes(size);

                for (size_t i = 0; i < size; i++) {
                    ResetNode(&nodes[i], static_cast<int>(i) * 10);
                    heap.Insert(&nodes[i]);
                }

                EXPECT_EQ(&nodes[victim], heap.Remove(&nodes[victim]));

                EXPECT_EQ(size - 1, Verify(heap))
                                    << "heap of " << size << " nodes, removed the one at position " << victim + 1;
            }
        }
    }

    TEST(MinHeapTest, RemoveEveryNodeInRandomOrder) {
        TestHeap heap{};
        std::mt19937 rng(20260818);
        std::vector<Node> nodes(128);
        std::vector<size_t> order(nodes.size());

        for (size_t i = 0; i < nodes.size(); i++) {
            ResetNode(&nodes[i], static_cast<int>(rng() % 64));
            heap.Insert(&nodes[i]);
            order[i] = i;
        }

        std::shuffle(order.begin(), order.end(), rng);

        for (size_t i = 0; i < order.size(); i++) {
            EXPECT_EQ(&nodes[order[i]], heap.Remove(&nodes[order[i]]));
            ASSERT_EQ(nodes.size() - i - 1, Verify(heap)) << "after removing " << i + 1 << " nodes";
        }
    }
} // namespace
