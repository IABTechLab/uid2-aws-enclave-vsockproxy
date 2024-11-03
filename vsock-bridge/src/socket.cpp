#include "logger.h"
#include "socket.h"

#include <cassert>
#include <cstring>

#include <sys/socket.h>

namespace vsockio
{
	Socket::Socket(int fd, SocketImpl& impl)
		: _fd(fd)
		, _impl(impl)
	{
		assert(_fd >= 0);
	}

	bool Socket::readFromInput()
	{
		if (_peer->outputClosed() && !inputClosed())
		{
			Logger::instance->Log(Logger::DEBUG, "[socket] readToInput detected output peer closed, closing input (fd=", _fd, ")");
			closeInput();
			return false;
		}

        if (!_connected) return false;

		if (_inputClosed) return false;

		const bool canReadMoreData = read(_peer->buffer());

		if (_inputClosed)
		{
			Logger::instance->Log(Logger::DEBUG, "[socket] readToInput detected input closed, closing (fd=", _fd, ")");
			close();
		}

		return canReadMoreData;
	}

	bool Socket::writeToOutput()
	{
        if (!_connected) return false;

		if (_outputClosed) return false;

        bool canSendModeData = false;
		if (!_outputClosed) {
            if (!_buffer.consumed()) {
                canSendModeData = send(_buffer);
                if (_buffer.consumed())
                {
                    _buffer.reset();
                }
            }
        }

		if (_peer->closed() && _buffer.consumed())
		{
            Logger::instance->Log(Logger::DEBUG, "[socket] writeToOutput finished draining socket, closing (fd=", _fd, ")");
            close();
		}

		return canSendModeData;
	}

	bool Socket::read(Buffer& buffer)
	{
        if (!buffer.hasRemainingCapacity()) return false;

        PERF_LOG("read");
        const int bytesRead = _impl.read(_fd, buffer.tail(), buffer.remainingCapacity());
        int err = 0;
        if (bytesRead > 0)
        {
            // New content read

            //Logger::instance->Log(Logger::DEBUG, "[socket] read returns ", bytesRead, " (fd=", _fd, ")");
            buffer.produce(bytesRead);
            return true;
        }
        else if (bytesRead == 0)
        {
            // Source closed

            Logger::instance->Log(Logger::DEBUG, "[socket] read returns 0, closing input (fd=", _fd, ")");
            closeInput();
            return false;
        }
        else if ((err = errno) == EAGAIN || err == EWOULDBLOCK)
        {
            // No new data

            return false;
        }
        else
        {
            // Error

            Logger::instance->Log(Logger::WARNING, "[socket] error on read, closing input (fd=", _fd, "): ", err, ", ", strerror(err));
            closeInput();
            return false;
        }
	}

	bool Socket::send(Buffer& buffer)
	{
        bool canSendModeData = false;
		while (!buffer.consumed())
		{
            PERF_LOG("send");
			const int bytesWritten = _impl.write(_fd, buffer.head(), buffer.remainingDataSize());

			int err = 0;
			if (bytesWritten > 0)
			{
				// Some data written to downstream
				// log bytes written and move cursor forward

				Logger::instance->Log(Logger::DEBUG, "[socket] write returns ", bytesWritten, " (fd=", _fd, ")");
				buffer.consume(bytesWritten);
                canSendModeData = true;
			}
			else if((err = errno) == EAGAIN || err == EWOULDBLOCK)
			{
				// Write blocked
				return false;
			}
			else
			{
				// Error

				Logger::instance->Log(Logger::WARNING, "[socket] error on send, closing (fd=", _fd, "): ", strerror(err));
				close();
				return false;
			}
		}

        return canSendModeData;
	}

    void Socket::checkConnected()
    {
        char c;
        const int bytesWritten = _impl.write(_fd, &c, 0);
        int err = errno;
        if (bytesWritten == 0)
        {
            _connected = true;
            Logger::instance->Log(Logger::WARNING, "[socket] connected (fd=", _fd, ")");
        }
        else if (err != EAGAIN && err != EWOULDBLOCK)
        {
            Logger::instance->Log(Logger::WARNING, "[socket] connection error, closing (fd=", _fd, "): ", err, ", ", strerror(err));
            close();
        }
    }

	void Socket::closeInput()
	{
		_inputClosed = true;
	}

	void Socket::close()
	{
		if (!closed())
		{
			_inputClosed = true;
			_outputClosed = true;

			if (_poller)
			{
				// epoll is meant to automatically deregister sockets on close, but apparently some systems
				// have bugs around this, so do it explicitly
				Logger::instance->Log(Logger::DEBUG, "[socket] remove from poller (fd=", _fd, ")");
				_poller->remove(_fd);
			}

			Logger::instance->Log(Logger::DEBUG, "[socket] close, fd=", _fd);
			_impl.close(_fd);
			if (_peer != nullptr)
			{
				_peer->onPeerClosed();
			}
		}
	}

	void Socket::onPeerClosed()
	{
		if (!closed())
		{
			Logger::instance->Log(Logger::DEBUG, "[socket] onPeerClosed draining socket (fd=", _fd, ")");

			// force process the output queue
			writeToOutput();

            if (_peer->hasQueuedData())
            {
                // Peer has some queued data they never received
                // Assuming this data is critical for the protocol, it should be ok to abort the connection straight away
                Logger::instance->Log(Logger::DEBUG, "[socket] onPeerClosed detected input peer is closed while having data remaining, closing (fd=", _fd, ")");
                close();
            }
		}
	}

	Socket::~Socket()
	{
		if (!closed())
		{
			Logger::instance->Log(Logger::WARNING, "[socket] closing on destruction (fd=", _fd, ")");
			close();
		}

		if (_peer != nullptr)
		{
			_peer->setPeer(nullptr);
		}
	}
}