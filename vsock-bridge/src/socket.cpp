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

			// There may be a termination buffer queued by the peer. Poller may not be able to detect that and mark
			// the socket as ready for write in a timely fashion. Force process the queue now.
			_outputReady = true;
			writeToOutput();

			return false;
		}

		if (_inputClosed) return false;

		bool hasInput = false;
		while (!_inputClosed && _inputReady && !_peer->queueFull())
		{
			std::unique_ptr<Buffer> buffer{ read() };
			if (buffer && !buffer->empty())
			{
				_peer->queue(std::move(buffer));
				hasInput = true;
			}
		}

		if (_inputClosed)
		{
			Logger::instance->Log(Logger::DEBUG, "[socket] readToInput detected input closed, closing (fd=", _fd, ")");
			close();
		}

		return hasInput;
	}

	bool Socket::writeToOutput()
	{
		if (_outputClosed) return false;

		while (!_outputClosed && _outputReady && !_sendQueue.empty())
		{
			std::unique_ptr<Buffer>& buffer = _sendQueue.front();

			// received termination signal from peer
			if (buffer->empty())
			{
				Logger::instance->Log(Logger::DEBUG, "[socket] writeToOutput dequeued a termination buffer (fd=", _fd, ")");
				_sendQueue.dequeue();
				close();
				break;
			}
			else
			{
				send(*buffer);
				if (buffer->consumed())
				{
					_sendQueue.dequeue();
					_queueFull = false;
				}
			}
		}

		if (_peer->closed())
		{
			if (_sendQueue.empty())
			{
				Logger::instance->Log(Logger::DEBUG, "[socket] writeToOutput detected input peer is closed, closing (fd=", _fd, ")");
				close();
			}
			else if (!_peer->queueEmpty())
			{
				// Peer has some queued data they never received
				// Assuming this data is critical for the protocol, it should be ok to abort the connection straight away
				Logger::instance->Log(Logger::DEBUG, "[socket] writeToOutput detected input peer is closed while having data remaining, closing (fd=", _fd, ")");
				close();
			}
		}

		return _sendQueue.empty();
	}

	void Socket::queue(std::unique_ptr<Buffer>&& buffer)
	{
		_sendQueue.enqueue(std::move(buffer));

		// to simplify logic we allow only 1 buffer for socket sinks
		_queueFull = true;
	}

	std::unique_ptr<Buffer> Socket::read()
	{
		std::unique_ptr<Buffer> buffer{ BufferManager::getBuffer() };
		int bytesRead;
		int totalBytes = 0;

		while (true)
		{
			bytesRead = _impl.read(_fd, buffer->tail(), buffer->remainingCapacity());
			int err = 0;
			if (bytesRead > 0)
			{
				// New content read
				// update byte count and enlarge buffer if needed

				//Logger::instance->Log(Logger::DEBUG, "[socket] read returns ", bytesRead, " (fd=", _fd, ")");
				buffer->produce(bytesRead);
				if (!buffer->ensureCapacity())
				{
					break;
				}
			}
			else if (bytesRead == 0)
			{
				// Source closed

				Logger::instance->Log(Logger::DEBUG, "[socket] read returns 0, closing input (fd=", _fd, ")");
				closeInput();
				break;
			}
			else if ((err = errno) == EAGAIN || err == EWOULDBLOCK)
			{
				// No new data

				_inputReady = false;
				break;
			}
			else
			{
				// Error

				Logger::instance->Log(Logger::WARNING, "[socket] error on read, closing input (fd=", _fd, "): ", strerror(err));
				closeInput();
				break;
			}
		}

		return buffer;
	}

	void Socket::send(Buffer& buffer)
	{
		int bytesWritten;
		while (!buffer.consumed())
		{
			bytesWritten = _impl.write(_fd, buffer.head(), buffer.headLimit());

			int err = 0;
			if (bytesWritten > 0)
			{
				// Some data written to downstream
				// log bytes written and move cursor forward

				//Logger::instance->Log(Logger::DEBUG, "[socket] write returns ", bytesWritten, " (fd=", _fd, ")");
				buffer.consume(bytesWritten);
			}
			else if((err = errno) == EAGAIN || err == EWOULDBLOCK)
			{
				// Write blocked
				_outputReady = false;
				break;
			}
			else
			{
				// Error

				Logger::instance->Log(Logger::WARNING, "[socket] error on send, closing (fd=", _fd, "): ", strerror(err));
				close();
				break;
			}
		}

	}

	void Socket::closeInput()
	{
		_inputClosed = true;
	}

	void Socket::close()
	{
		_inputReady = false;
		_outputReady = false;

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
			Logger::instance->Log(Logger::DEBUG, "[socket] sending termination for (fd=", _fd, ")");
			std::unique_ptr<Buffer> termination{ BufferManager::getEmptyBuffer() };
			queue(std::move(termination));

			// force process the queue
			_outputReady = true;
			writeToOutput();
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