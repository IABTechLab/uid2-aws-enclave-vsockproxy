#include <threading.h>

#include "catch.hpp"

using namespace vsockio;

SCENARIO("ThreadSafeQueue")
{
    GIVEN("A newly created queue")
    {
        ThreadSafeQueue<int> q;
        THEN("Dequeue returns empty")
        {
            REQUIRE(!q.dequeue());
        }
    }

    GIVEN("A queue with starting items")
    {
        ThreadSafeQueue<int> q;
        q.enqueue(1);
        q.enqueue(2);

        THEN("Can dequeue the first element")
        {
            REQUIRE(q.dequeue() == 1);

            AND_THEN("Can dequeue the second element")
            {
                REQUIRE(q.dequeue() == 2);

                AND_THEN("Cannot dequeue more")
                {
                    REQUIRE(!q.dequeue());
                }
            }
        }
    }

    GIVEN("A queue with starting items dequeued")
    {
        ThreadSafeQueue<int> q;
        q.enqueue(1);
        q.enqueue(2);
        q.dequeue();
        q.dequeue();

        THEN("Can enqueue and dequeue another item")
        {
            q.enqueue(3);
            REQUIRE(q.dequeue() == 3);

            AND_THEN("Cannot dequeue more")
            {
                REQUIRE(!q.dequeue());
            }
        }
    }
}
