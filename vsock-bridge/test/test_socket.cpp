#include <channel.h>

#include "catch.hpp"

using namespace vsockio;

static int mockIoAgain(int, void*, int)
{
    errno = EAGAIN;
    return -1;
}

static std::function<int (int, void*, int)> mockIoSuccessOnce(int returnValue)
{
    bool called = false;
    return [=] (int fd, void* data, int sz) mutable
        {
            if (!called)
            {
                called = true;
                return returnValue;
            }
            else
            {
                return mockIoAgain(fd, data, sz);
            }
        };
}

static std::function<int (int, void*, int)> mockIoMustNotCall(const std::string& message)
{
    return [=] (int, void*, int) { FAIL(message); return -1; };
}

static std::function<int (int, void*, int)> mockIoError(int err)
{
    return [=] (int, void*, int) { errno = err; return -1; };
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
            REQUIRE(!channel.canReadWriteMore());
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
            REQUIRE(!channel.canReadWriteMore());
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
        saImpl.read = mockIoSuccessOnce(5);

        THEN("Read all and queue for writing")
        {
            channel.performIO();
            REQUIRE(channel.canReadWriteMore());
            REQUIRE(!channel.canBeTerminated());

            AND_THEN("Read socket has pending IO, but writer does not")
            {
                REQUIRE(sa.canReadWriteMore());
                REQUIRE(!sb.canReadWriteMore());
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
        saImpl.read = mockIoSuccessOnce(5);
        sbImpl.write = [&] (int fd, void* d, int sz) { REQUIRE(sz == 5); return mockIoAgain(fd, d, sz); };

        THEN("Read all and queue for writing")
        {
            channel.performIO();
            REQUIRE(channel.canReadWriteMore());
            REQUIRE(!channel.canBeTerminated());

            AND_THEN("Read socket has pending IO, but writer does not")
            {
                REQUIRE(sa.canReadWriteMore());
                REQUIRE(!sb.canReadWriteMore());
            }
        }
    }

    GIVEN("Some data available on one of the sockets and some write succeeds on the other")
    {
        saImpl.read = mockIoSuccessOnce(5);
        sbImpl.write = mockIoSuccessOnce(3);

        THEN("Read all and queue for writing")
        {
            channel.performIO();
            REQUIRE(channel.canReadWriteMore());
            REQUIRE(!channel.canBeTerminated());

            AND_THEN("Both sockets have pending IO")
            {
                REQUIRE(sa.canReadWriteMore());
                REQUIRE(!sb.canReadWriteMore());
            }

            AND_THEN("Remaining data is written")
            {
                saImpl.read = mockIoAgain;
                sbImpl.write = mockIoSuccessOnce(2);

                channel.performIO();
                REQUIRE(channel.canReadWriteMore());
                REQUIRE(!channel.canBeTerminated());

                REQUIRE(!sa.canReadWriteMore());
                REQUIRE(sb.canReadWriteMore());
            }
        }
    }

    GIVEN("Write queue is full on one of the sockets")
    {
        saImpl.read = mockIoSuccessOnce(Buffer::BUFFER_SIZE);
        channel.performIO();

        THEN("No reads are performed afterwards")
        {
            saImpl.read = mockIoMustNotCall("sa read");
            channel.performIO();
            REQUIRE(!channel.canReadWriteMore());

            AND_THEN("No reads are performed even after some of the data has been written out")
            {
                sbImpl.write = mockIoSuccessOnce(2);
                channel.performIO();
                REQUIRE(!channel.canReadWriteMore());
            }
        }

        THEN("Can resume reads after the buffer has been fully consumed")
        {
            saImpl.read = mockIoMustNotCall("sa read");
            sbImpl.write = mockIoSuccessOnce(Buffer::BUFFER_SIZE);
            channel.performIO();
            REQUIRE(channel.canReadWriteMore());
            REQUIRE(!sa.canReadWriteMore());
            REQUIRE(sb.canReadWriteMore());

            saImpl.read = mockIoSuccessOnce(3);
            channel.performIO();
            REQUIRE(channel.canReadWriteMore());
            REQUIRE(sa.canReadWriteMore());
            REQUIRE(!sb.canReadWriteMore());
        }
    }

    GIVEN("Socket writes out full buffer of data")
    {
        saImpl.read = mockIoSuccessOnce(Buffer::BUFFER_SIZE);
        channel.performIO();
        sbImpl.write = mockIoSuccessOnce(Buffer::BUFFER_SIZE);
        channel.performIO();

        THEN("Socket can write more data")
        {
            saImpl.read = mockIoSuccessOnce(5);
            int bytesWritten = 0;
            sbImpl.write = [&] (int fd, void* d, int sz) { bytesWritten = sz; return mockIoAgain(fd, d, sz); };
            channel.performIO();

            REQUIRE(bytesWritten == 5);
        }
    }
}

SCENARIO("DirectChannel - async connection")
{
    SocketImpl saImpl(mockIoAgain, mockIoAgain, mockCloseSuccess);
    SocketImpl sbImpl(mockIoAgain, mockIoAgain, mockCloseSuccess);
    DirectChannel channel(1, std::make_unique<Socket>(41, saImpl), std::make_unique<Socket>(42, sbImpl));
    auto &sa = *channel._a;
    auto &sb = *channel._b;
    sa.onConnected();

    GIVEN("Second socket reports a successful connection")
    {
        sbImpl.write = mockIoSuccessOnce(0);
        sb.checkConnected();

        THEN("Second socket is connected")
        {
            REQUIRE(sb.connected());
        }
    }
}

SCENARIO("DirectChannel - orderly disconnects")
{
    SocketImpl saImpl(mockIoAgain, mockIoAgain, mockCloseSuccess);
    SocketImpl sbImpl(mockIoAgain, mockIoAgain, mockCloseSuccess);
    DirectChannel channel(1, std::make_unique<Socket>(41, saImpl), std::make_unique<Socket>(42, sbImpl));
    auto &sa = *channel._a;
    auto &sb = *channel._b;
    sa.onConnected();
    sb.onConnected();

    GIVEN("A socket reports it is closed while no outstanding data")
    {
        sbImpl.read = mockIoSuccessOnce(0);
        channel.performIO();

        THEN("Second socket is disconnected")
        {
            REQUIRE(sa.closed());
            REQUIRE(sb.closed());
            REQUIRE(channel.canBeTerminated());
        }
    }

    GIVEN("A socket reports it is closed while second socket is writing data out")
    {
        saImpl.read = mockIoSuccessOnce(10);
        channel.performIO();
        saImpl.read = mockIoSuccessOnce(0);
        channel.performIO();

        THEN("First socket is closed and second socket is not")
        {
            REQUIRE(sa.closed());
            REQUIRE(!sb.closed());

            AND_THEN("Second socket writes out some data and remains open")
            {
                sbImpl.write = mockIoSuccessOnce(6);
                channel.performIO();

                REQUIRE(sa.closed());
                REQUIRE(!sb.closed());

                AND_THEN("Second socket writes out remaining data and both sockets are closed")
                {
                    sbImpl.write = mockIoSuccessOnce(4);
                    channel.performIO();

                    REQUIRE(sa.closed());
                    REQUIRE(sb.closed());
                }
            }
        }

        THEN("Second socket input is closed")
        {
            sbImpl.read = mockIoMustNotCall("sb read");
            channel.performIO();

            REQUIRE(sa.closed());
            REQUIRE(!sb.closed());
        }
    }

    GIVEN("A socket reports it is closed while it is writing data out")
    {
        saImpl.read = mockIoSuccessOnce(10);
        channel.performIO();
        sbImpl.read = mockIoSuccessOnce(0);
        channel.performIO();

        THEN("Both sockets are closed")
        {
            REQUIRE(sa.closed());
            REQUIRE(sb.closed());
        }
    }
}

SCENARIO("DirectChannel - error conditions")
{
    SocketImpl saImpl(mockIoAgain, mockIoAgain, mockCloseSuccess);
    SocketImpl sbImpl(mockIoAgain, mockIoAgain, mockCloseSuccess);
    DirectChannel channel(1, std::make_unique<Socket>(41, saImpl), std::make_unique<Socket>(42, sbImpl));
    auto &sa = *channel._a;
    auto &sb = *channel._b;
    sa.onConnected();

    GIVEN("Second socket reports a connection error")
    {
        sbImpl.write = mockIoError(ECONNREFUSED);
        sb.checkConnected();

        THEN("Both sockets are closed")
        {
            REQUIRE(channel.canBeTerminated());
            REQUIRE(sa.closed());
            REQUIRE(sb.closed());
        }
    }

    sb.onConnected();

    GIVEN("Socket read fails")
    {
        saImpl.read = mockIoError(ECONNABORTED);
        channel.performIO();

        THEN("Both sockets are closed")
        {
            REQUIRE(channel.canBeTerminated());
            REQUIRE(sa.closed());
            REQUIRE(sb.closed());
        }
    }

    GIVEN("Second socket has data queued for writing")
    {
        saImpl.read = mockIoSuccessOnce(10);
        channel.performIO();

        AND_GIVEN("Reading the first second fails")
        {
            saImpl.read = mockIoError(ECONNABORTED);
            channel.performIO();

            THEN("Second socket enters draining mode")
            {
                REQUIRE(!channel.canBeTerminated());
                REQUIRE(sa.closed());
                REQUIRE(!sb.closed());

                AND_THEN("Writes out all queued data and is closed")
                {
                    sbImpl.write = mockIoSuccessOnce(10);
                    channel.performIO();

                    REQUIRE(channel.canBeTerminated());
                    REQUIRE(sa.closed());
                    REQUIRE(sb.closed());
                }
            }
        }
    }

    GIVEN("Socket write fails")
    {
        saImpl.read = mockIoSuccessOnce(10);
        channel.performIO();
        sbImpl.write = mockIoError(ECONNABORTED);
        channel.performIO();

        THEN("Both sockets are closed")
        {
            REQUIRE(channel.canBeTerminated());
            REQUIRE(sa.closed());
            REQUIRE(sb.closed());
        }
    }

    GIVEN("Socket write fails while draining")
    {
        saImpl.read = mockIoSuccessOnce(10);
        channel.performIO();
        saImpl.read = mockIoSuccessOnce(0);
        channel.performIO();
        sbImpl.write = mockIoError(ECONNABORTED);
        channel.performIO();

        THEN("Both sockets are closed")
        {
            REQUIRE(channel.canBeTerminated());
            REQUIRE(sa.closed());
            REQUIRE(sb.closed());
        }
    }
}
