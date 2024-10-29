#include <buffer.h>

#include "catch.hpp"

using namespace vsockio;

SCENARIO("Buffer")
{
    Buffer buffer;

    GIVEN("Newly created buffer")
    {
        THEN("Buffer has basic initial state")
        {
            REQUIRE(buffer.hasRemainingCapacity());
            REQUIRE(buffer.remainingCapacity() == Buffer::BUFFER_SIZE);
            REQUIRE(buffer.remainingDataSize() == 0);
            REQUIRE(buffer.consumed());
        }
    }

    GIVEN("Some data produced into the buffer")
    {
        buffer.produce(5);

        THEN("Buffer tail shifts, but head stays in place")
        {
            REQUIRE(buffer.head() == buffer._data.data());
            REQUIRE(buffer.tail() == buffer._data.data() + 5);
            REQUIRE(buffer.hasRemainingCapacity());
            REQUIRE(buffer.remainingCapacity() == Buffer::BUFFER_SIZE - 5);
            REQUIRE(buffer.remainingDataSize() == 5);
            REQUIRE(!buffer.consumed());
        }
    }

    GIVEN("Some data produced into the buffer and then partially consumed")
    {
        buffer.produce(5);
        buffer.consume(3);

        THEN("Buffer head and tail shift accordingly")
        {
            REQUIRE(buffer.head() == buffer._data.data() + 3);
            REQUIRE(buffer.tail() == buffer._data.data() + 5);
            REQUIRE(buffer.hasRemainingCapacity());
            REQUIRE(buffer.remainingCapacity() == Buffer::BUFFER_SIZE - 5);
            REQUIRE(buffer.remainingDataSize() == 2);
            REQUIRE(!buffer.consumed());
        }
    }

    GIVEN("Some data produced into the buffer and then fully consumed")
    {
        buffer.produce(5);
        buffer.consume(5);

        THEN("Buffer head and tail shift accordingly")
        {
            REQUIRE(buffer.head() == buffer._data.data() + 5);
            REQUIRE(buffer.tail() == buffer._data.data() + 5);
            REQUIRE(buffer.hasRemainingCapacity());
            REQUIRE(buffer.remainingCapacity() == Buffer::BUFFER_SIZE - 5);
            REQUIRE(buffer.remainingDataSize() == 0);
            REQUIRE(buffer.consumed());
        }
    }

    GIVEN("Buffer is completely filled with data")
    {
        buffer.produce(Buffer::BUFFER_SIZE);

        THEN("Buffer does not have remaining capacity")
        {
            REQUIRE(buffer.head() == buffer._data.data());
            REQUIRE(buffer.tail() == buffer._data.data() + Buffer::BUFFER_SIZE);
            REQUIRE(!buffer.hasRemainingCapacity());
            REQUIRE(buffer.remainingCapacity() == 0);
            REQUIRE(buffer.remainingDataSize() == Buffer::BUFFER_SIZE);
            REQUIRE(!buffer.consumed());
        }
    }

    GIVEN("Buffer in non-default state")
    {
        buffer.produce(5);
        buffer.consume(3);

        THEN("Reset restpres the default state")
        {
            buffer.reset();
            REQUIRE(buffer.head() == buffer._data.data());
            REQUIRE(buffer.tail() == buffer._data.data());
            REQUIRE(buffer.hasRemainingCapacity());
            REQUIRE(buffer.remainingCapacity() == Buffer::BUFFER_SIZE);
            REQUIRE(buffer.remainingDataSize() == 0);
            REQUIRE(buffer.consumed());
        }
    }
}
