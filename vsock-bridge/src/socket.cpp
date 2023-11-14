#include <socket.h>
#include <logger.h>
#include <cstring>

namespace vsockio
{
	Socket::Socket(int fd, SocketImpl* impl)
		: _fd(fd)
		, _impl(impl)
		, Peer(false, false)
	{}

	void Socket::readFromInput(bool& continuation)
	{
		continuation = false;

		if (_peer->closed()) 
		{
			Logger::instance->Log(Logger::DEBUG, "shutdown 1");
			shutdown();
		}
		if (_inputClosed) return;

		while (!_inputClosed && _inputReady && !_peer->queueFull())
		{
			std::unique_ptr<Buffer> buffer{ read() };
			if (buffer != nullptr && buffer->size() > 0)
			{
				_peer->queue(buffer);
				continuation = true;
			}
		}

		if (_inputClosed && !_peer->outputClosed())
		{
			Logger::instance->Log(Logger::DEBUG, "[socket] sending termination from (fd=", _fd, ")");
			std::unique_ptr<Buffer> termination{ BufferManager::getEmptyBuffer() };
			_peer->queue(termination);

			Logger::instance->Log(Logger::DEBUG, "shutdown 2");
			shutdown();
			continuation = true;
		}
	}

	void Socket::writeToOutput(bool& continuation)
	{
		continuation = false;

		if (_outputClosed) return;
		while (!_outputClosed && _outputReady && !_sendQueue.empty())
		{
			std::unique_ptr<Buffer>& buffer = _sendQueue.front();

			// received termination signal from peer
			if (buffer->size() == 0)
			{
				Logger::instance->Log(Logger::DEBUG, "[socket] writeToOutput dequeued a termination buffer (fd=", _fd, ")");
				Logger::instance->Log(Logger::DEBUG, "shutdown 3");
				shutdown();
				break;
			}
			else
			{
				send(buffer);
				if (buffer->cursor() == buffer->size())
				{
					_sendQueue.dequeue();
					_queueFull = false;
				}
			}
		}

		if (_peer->closed() && _sendQueue.empty())
		{
			Logger::instance->Log(Logger::DEBUG, "shutdown 4");
			shutdown();
		}

		continuation = _sendQueue.empty();
	}

	void Socket::queue(std::unique_ptr<Buffer>& buffer)
	{
		_sendQueue.enqueue(buffer);

		// to simplify logic we allow only 1 buffer for socket sinks
		_queueFull = true;
	}

	std::unique_ptr<Buffer> Socket::read()
	{
		std::unique_ptr<Buffer> buffer{ BufferManager::getBuffer() };
		ssize_t bytesRead;
		ssize_t totalBytes = 0;

		while (true)
		{
			bytesRead = _impl->read(_fd, buffer->offset(totalBytes), (ssize_t)buffer->capacity() - totalBytes);
			int err = 0;
			if (bytesRead > 0)
			{
				// New content read
				// update byte count and enlarge buffer if needed

				totalBytes += bytesRead;
				if (totalBytes == buffer->capacity() && !buffer->tryNewPage())
				{
					break;
				}
			}
			else if (bytesRead == 0)
			{
				// Source closed
				// Shutdown ourself and queue close message to peer

				Logger::instance->Log(Logger::DEBUG, "[socket] read returns 0 (fd=", _fd, ")");
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

				Logger::instance->Log(Logger::WARNING, "error on read: ", strerror(err));
				closeInput();
				break;
			}
		}

		buffer->setSize(totalBytes);
		buffer->setCursor(0);
		return buffer;
	}

	void Socket::send(std::unique_ptr<Buffer>& buffer)
	{
		ssize_t bytesWritten;
		ssize_t totalBytes = buffer->cursor();
		while (true)
		{
			ssize_t pageLimit = buffer->pageLimit(totalBytes);
			ssize_t dataRemaining = buffer->size() - totalBytes;
			ssize_t lengthToWrite = pageLimit < dataRemaining ? pageLimit : dataRemaining;
			bytesWritten = _impl->write(_fd, buffer->offset(totalBytes), lengthToWrite);

			int err = 0;
			if (bytesWritten > 0)
			{
				// Some data written to downstream
				// log bytes written and move cursor forward

				totalBytes += bytesWritten;
				buffer->setCursor(totalBytes);
				if (totalBytes == buffer->size())
				{
					break;
				}
			}
			else if((err = errno) == EAGAIN || err == EWOULDBLOCK)
			{
				// Write blocked
				buffer->setCursor(totalBytes);
				_outputReady = false;
				break;
			}
			else
			{
				// Error

				Logger::instance->Log(Logger::WARNING, "error on send: ", strerror(err));
				buffer->setCursor(totalBytes);
				Logger::instance->Log(Logger::DEBUG, "shutdown 5");
				shutdown();
				break;
			}
		}

	}

	void Socket::closeInput()
	{
		_inputClosed = true;
	}

	void Socket::closeOutput()
	{
		if (_inputClosed)
		{
			Logger::instance->Log(Logger::DEBUG, "shutdown 6");
			shutdown();
		}
	}

	void Socket::shutdown()
	{
		_inputReady = false;
		_outputReady = false;

		if (!closed())
		{
			_inputClosed = true;
			_outputClosed = true;
			Logger::instance->Log(Logger::DEBUG, "socket shutdown, fd=", _fd);

			_impl->close(_fd);
			if (_peer != nullptr)
			{
				_peer->onPeerShutdown();
			}
		}
	}

	void Socket::onPeerShutdown()
	{
		if (!closed())
		{
			Logger::instance->Log(Logger::DEBUG, "[socket] sending termination from (fd=", _fd, ")");
			std::unique_ptr<Buffer> termination{ BufferManager::getEmptyBuffer() };
			queue(termination);
			bool _;
			writeToOutput(_);
		}
	}

	Socket::~Socket() {}
}