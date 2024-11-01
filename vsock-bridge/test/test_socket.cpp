#include <channel.h>

#include "catch.hpp"

using namespace vsockio;

static int mockIoAgain(int, void*, int)
{
    errno = EAGAIN;
    return -1;
}

static std::function<int (int, void*, int)> mockIoSuccess(int returnValue)
{
    return [=] (int, void*, int) { return returnValue; };
}

static std::function<int (int, void*, int)> mockIoMustNotCall(const std::string& message)
{
    return [=] (int, void*, int) { FAIL(message); return -1; };
}

static int mockCloseSuccess(int)
{
    return 0;
}

SCENARIO("DirectChannel between sockets establishing connections")
{
    SocketImpl saImpl(mockIoMustNotCall("read on sa"), mockIoMustNotCall("write on sa"), mockCloseSuccess);
    SocketImpl sbImpl(mockIoMustNotCall("read on sb"), mockIoMustNotCall("write on sa"), mockCloseSuccess);
    DirectChannel channel(1, std::make_unique<Socket>(41, saImpl), std::make_unique<Socket>(42, sbImpl));
    auto &sa = *channel._a;
    auto &sb = *channel._b;

    GIVEN("Sockets are not connected")
    {
        THEN("No IO is possible")
        {
            channel.performIO();
            REQUIRE(!channel.hasPendingIO());
            REQUIRE(!channel.canBeTerminated());
        }
    }

    GIVEN("Both sockets connected, but no data is available")
    {
        sa.onConnected();
        sb.onConnected();

        saImpl.read = sbImpl.read = mockIoAgain;

        THEN("No remaining IO after reads")
        {
            channel.performIO();
            REQUIRE(!channel.hasPendingIO());
            REQUIRE(!channel.canBeTerminated());
        }
    }
}

SCENARIO("DirectChannel between sockets with one connected socket")
{
    SocketImpl saImpl(mockIoMustNotCall("read on sa"), mockIoMustNotCall("write on sa"), mockCloseSuccess);
    SocketImpl sbImpl(mockIoMustNotCall("read on sb"), mockIoMustNotCall("write on sa"), mockCloseSuccess);
    DirectChannel channel(1, std::make_unique<Socket>(41, saImpl), std::make_unique<Socket>(42, sbImpl));
    auto &sa = *channel._a;
    auto &sb = *channel._b;
    sa.onConnected();

    GIVEN("Some data available on the connected socket")
    {
        saImpl.read = mockIoSuccess(5);

        THEN("Read all and queue for writing")
        {
            channel.performIO();
            REQUIRE(channel.hasPendingIO());
            REQUIRE(!channel.canBeTerminated());

            AND_THEN("Read socket has pending IO, but writer does not")
            {
                REQUIRE(sa.hasPendingIO());
                REQUIRE(!sb.hasPendingIO());
            }
        }
    }
}

SCENARIO("DirectChannel between connected sockets")
{
    SocketImpl saImpl(mockIoAgain, mockIoAgain, mockCloseSuccess);
    SocketImpl sbImpl(mockIoAgain, mockIoAgain, mockCloseSuccess);
    DirectChannel channel(1, std::make_unique<Socket>(41, saImpl), std::make_unique<Socket>(42, sbImpl));
    auto &sa = *channel._a;
    auto &sb = *channel._b;
    sa.onConnected();
    sb.onConnected();

    GIVEN("Some data available on one of the sockets but write is blocked on the other")
    {
        saImpl.read = mockIoSuccess(5);
        sbImpl.write = [&] (int fd, void* d, int sz) { REQUIRE(sz == 5); return mockIoAgain(fd, d, sz); };

        THEN("Read all and queue for writing")
        {
            channel.performIO();
            REQUIRE(channel.hasPendingIO());
            REQUIRE(!channel.canBeTerminated());

            AND_THEN("Read socket has pending IO, but writer does not")
            {
                REQUIRE(sa.hasPendingIO());
                REQUIRE(!sb.hasPendingIO());
            }
        }
    }

    GIVEN("Some data available on one of the sockets and some write succeeds on the other")
    {
        saImpl.read = mockIoSuccess(5);
        sbImpl.write = mockIoSuccess(3);

        THEN("Read all and queue for writing")
        {
            channel.performIO();
            REQUIRE(channel.hasPendingIO());
            REQUIRE(!channel.canBeTerminated());

            AND_THEN("Both sockets have pending IO")
            {
                REQUIRE(sa.hasPendingIO());
                REQUIRE(sb.hasPendingIO());
            }
        }
    }
}
