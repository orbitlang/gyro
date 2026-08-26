// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <vector>

#include <gtest/gtest.h>

#include <support/queue.h>

namespace {
    struct Node {
        struct {
            Node *next;
            Node *prev;
        } queue;

        int value;
    };

    using TestQueue = gyro::support::Queue<Node>;

    Node MakeNode(int value) {
        Node node{};

        node.value = value;

        return node;
    }

    /**
     * @brief Walks the queue both ways and checks it is intact.
     *
     * Splicing bugs leave a list that still reads correctly from the head but
     * whose prev chain no longer matches, so both directions are verified.
     *
     * @return The values found walking head to tail.
     */
    std::vector<int> Verify(const TestQueue &queue) {
        std::vector<int> forward;
        std::vector<Node *> seen;

        for (Node *node = queue.GetHead(); node != nullptr; node = node->queue.next) {
            forward.push_back(node->value);
            seen.push_back(node);

            if (seen.size() > 1024) {
                ADD_FAILURE() << "cycle in the queue";

                return forward;
            }
        }

        EXPECT_EQ(forward.size(), queue.Count()) << "Count() disagrees with the list";

        // The prev chain must mirror the next chain exactly.
        for (size_t i = 0; i < seen.size(); i++) {
            Node *expected = i == 0 ? nullptr : seen[i - 1];

            EXPECT_EQ(expected, seen[i]->queue.prev) << "broken prev link at position " << i;
        }

        return forward;
    }

    TEST(QueueTest, NewQueueIsEmpty) {
        TestQueue queue;

        EXPECT_EQ(0u, queue.Count());
        EXPECT_EQ(nullptr, queue.GetHead());
    }

    TEST(QueueTest, DequeueOnAnEmptyQueueReturnsNull) {
        TestQueue queue;

        EXPECT_EQ(nullptr, queue.Dequeue());
        EXPECT_EQ(0u, queue.Count());
    }

    TEST(QueueTest, EnqueueThenDequeueOneNode) {
        TestQueue queue;
        Node node = MakeNode(7);

        queue.Enqueue(&node);

        EXPECT_EQ(1u, queue.Count());
        EXPECT_EQ(&node, queue.GetHead());
        EXPECT_EQ(&node, queue.Dequeue());
        EXPECT_EQ(0u, queue.Count());
        EXPECT_EQ(nullptr, queue.GetHead());
    }

    TEST(QueueTest, GetHeadDoesNotRemove) {
        TestQueue queue;
        Node node = MakeNode(1);

        queue.Enqueue(&node);

        EXPECT_EQ(&node, queue.GetHead());
        EXPECT_EQ(&node, queue.GetHead());
        EXPECT_EQ(1u, queue.Count());
    }

    TEST(QueueTest, PreservesFifoOrder) {
        TestQueue queue;
        std::vector<Node> nodes;

        nodes.reserve(5);
        for (int i = 0; i < 5; i++)
            nodes.push_back(MakeNode(i));

        for (auto &node: nodes)
            queue.Enqueue(&node);

        ASSERT_EQ(5u, queue.Count());

        for (int i = 0; i < 5; i++) {
            Node *node = queue.Dequeue();

            ASSERT_NE(nullptr, node) << "at position " << i;
            EXPECT_EQ(i, node->value) << "dequeued out of order at position " << i;
        }

        EXPECT_EQ(nullptr, queue.Dequeue());
    }

    TEST(QueueTest, CountTracksTheNumberOfNodes) {
        TestQueue queue;
        std::vector<Node> nodes;

        nodes.reserve(3);
        for (int i = 0; i < 3; i++)
            nodes.push_back(MakeNode(i));

        EXPECT_EQ(0u, queue.Count());

        queue.Enqueue(&nodes[0]);
        EXPECT_EQ(1u, queue.Count());

        queue.Enqueue(&nodes[1]);
        EXPECT_EQ(2u, queue.Count());

        queue.Dequeue();
        EXPECT_EQ(1u, queue.Count());

        queue.Enqueue(&nodes[2]);
        EXPECT_EQ(2u, queue.Count());

        queue.Dequeue();
        queue.Dequeue();
        EXPECT_EQ(0u, queue.Count());
    }

    TEST(QueueTest, DequeueClearsTheNodeLinks) {
        TestQueue queue;
        Node first = MakeNode(1);
        Node second = MakeNode(2);

        queue.Enqueue(&first);
        queue.Enqueue(&second);

        Node *node = queue.Dequeue();

        ASSERT_EQ(&first, node);
        EXPECT_EQ(nullptr, node->queue.next) << "a dequeued node must not point back into the queue";
        EXPECT_EQ(nullptr, node->queue.prev);
    }

    /**
     * Draining a queue must leave it exactly as new: a stale tail_ pointer only
     * shows up on the enqueue that follows.
     */
    TEST(QueueTest, IsReusableAfterDraining) {
        TestQueue queue;
        Node first = MakeNode(1);
        Node second = MakeNode(2);
        Node third = MakeNode(3);

        queue.Enqueue(&first);
        ASSERT_EQ(&first, queue.Dequeue());
        ASSERT_EQ(0u, queue.Count());

        queue.Enqueue(&second);
        queue.Enqueue(&third);

        EXPECT_EQ(2u, queue.Count());
        EXPECT_EQ(&second, queue.Dequeue());
        EXPECT_EQ(&third, queue.Dequeue());
        EXPECT_EQ(nullptr, queue.Dequeue());
    }

    TEST(QueueTest, SurvivesInterleavedEnqueueAndDequeue) {
        TestQueue queue;
        std::vector<Node> nodes;

        nodes.reserve(64);
        for (int i = 0; i < 64; i++)
            nodes.push_back(MakeNode(i));

        size_t enqueued = 0;
        size_t dequeued = 0;

        while (dequeued < nodes.size()) {
            // Keep two in flight, so head and tail are exercised together.
            while (enqueued < nodes.size() && queue.Count() < 2)
                queue.Enqueue(&nodes[enqueued++]);

            Node *node = queue.Dequeue();

            ASSERT_NE(nullptr, node) << "after " << dequeued << " dequeues";
            EXPECT_EQ(static_cast<int>(dequeued), node->value);

            dequeued++;
        }

        EXPECT_EQ(0u, queue.Count());
        EXPECT_EQ(nullptr, queue.Dequeue());
    }

    TEST(QueueTest, RemoveFromTheMiddle) {
        TestQueue queue;
        std::vector<Node> nodes;

        nodes.reserve(4);
        for (int i = 0; i < 4; i++)
            nodes.push_back(MakeNode(i));

        for (auto &node: nodes)
            queue.Enqueue(&node);

        queue.Remove(&nodes[1]);

        EXPECT_EQ(3u, queue.Count());
        EXPECT_EQ(std::vector<int>({0, 2, 3}), Verify(queue));
    }

    TEST(QueueTest, RemoveTheHead) {
        TestQueue queue;
        std::vector<Node> nodes;

        nodes.reserve(3);
        for (int i = 0; i < 3; i++)
            nodes.push_back(MakeNode(i));

        for (auto &node: nodes)
            queue.Enqueue(&node);

        queue.Remove(&nodes[0]);

        EXPECT_EQ(&nodes[1], queue.GetHead());
        EXPECT_EQ(std::vector<int>({1, 2}), Verify(queue));
    }

    TEST(QueueTest, RemoveTheTail) {
        TestQueue queue;
        std::vector<Node> nodes;

        nodes.reserve(3);
        for (int i = 0; i < 3; i++)
            nodes.push_back(MakeNode(i));

        for (auto &node: nodes)
            queue.Enqueue(&node);

        queue.Remove(&nodes[2]);

        EXPECT_EQ(std::vector<int>({0, 1}), Verify(queue));

        // A stale tail_ only shows up on the next enqueue.
        Node added = MakeNode(9);
        queue.Enqueue(&added);

        EXPECT_EQ(std::vector<int>({0, 1, 9}), Verify(queue));
    }

    TEST(QueueTest, RemoveTheOnlyNode) {
        TestQueue queue;
        Node node = MakeNode(1);

        queue.Enqueue(&node);
        queue.Remove(&node);

        EXPECT_EQ(0u, queue.Count());
        EXPECT_EQ(nullptr, queue.GetHead());
        EXPECT_EQ(nullptr, queue.Dequeue());

        // And the queue must be usable again afterwards.
        Node other = MakeNode(2);
        queue.Enqueue(&other);

        EXPECT_EQ(std::vector<int>({2}), Verify(queue));
    }

    TEST(QueueTest, RemoveClearsTheNodeLinks) {
        TestQueue queue;
        std::vector<Node> nodes;

        nodes.reserve(3);
        for (int i = 0; i < 3; i++)
            nodes.push_back(MakeNode(i));

        for (auto &node: nodes)
            queue.Enqueue(&node);

        queue.Remove(&nodes[1]);

        EXPECT_EQ(nullptr, nodes[1].queue.next) << "a removed node must not point back into the queue";
        EXPECT_EQ(nullptr, nodes[1].queue.prev);
    }

    TEST(QueueTest, RemoveEveryNodeInEveryOrder) {
        constexpr int kSize = 5;

        for (int victim = 0; victim < kSize; victim++) {
            TestQueue queue;
            std::vector<Node> nodes;

            nodes.reserve(kSize);
            for (int i = 0; i < kSize; i++)
                nodes.push_back(MakeNode(i));

            for (auto &node: nodes)
                queue.Enqueue(&node);

            queue.Remove(&nodes[victim]);

            std::vector<int> expected;
            for (int i = 0; i < kSize; i++) {
                if (i != victim)
                    expected.push_back(i);
            }

            EXPECT_EQ(expected, Verify(queue)) << "after removing position " << victim;
        }
    }

    TEST(QueueTest, RemoveAndDequeueCanBeMixed) {
        TestQueue queue;
        std::vector<Node> nodes;

        nodes.reserve(6);
        for (int i = 0; i < 6; i++)
            nodes.push_back(MakeNode(i));

        for (auto &node: nodes)
            queue.Enqueue(&node);

        queue.Remove(&nodes[3]);
        EXPECT_EQ(&nodes[0], queue.Dequeue());

        queue.Remove(&nodes[5]);
        EXPECT_EQ(&nodes[1], queue.Dequeue());

        EXPECT_EQ(std::vector<int>({2, 4}), Verify(queue));
        EXPECT_EQ(&nodes[2], queue.Dequeue());
        EXPECT_EQ(&nodes[4], queue.Dequeue());
        EXPECT_EQ(nullptr, queue.Dequeue());
    }

    TEST(QueueTest, EmptiesCompletelyThroughRemove) {
        TestQueue queue;
        std::vector<Node> nodes;

        nodes.reserve(4);
        for (int i = 0; i < 4; i++)
            nodes.push_back(MakeNode(i));

        for (auto &node: nodes)
            queue.Enqueue(&node);

        // Back to front: exercises the tail_ update every time.
        for (int i = 3; i >= 0; i--) {
            queue.Remove(&nodes[i]);

            ASSERT_EQ(static_cast<unsigned int>(i), queue.Count());
            Verify(queue);
        }

        EXPECT_EQ(nullptr, queue.GetHead());
        EXPECT_EQ(nullptr, queue.Dequeue());
    }
} // namespace
