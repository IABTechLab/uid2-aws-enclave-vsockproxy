#pragma once

#include "buffer.h"
#include "poller.h"

#include <cassert>
#include <functional>
#include <memory>

namespace vsockio
{
	struct SocketImpl
	{
		static SocketImpl* singleton;

		std::function<int(int, void*, int)> read;
		std::function<int(int, void*, int)> write;
		std::function<int(int)> close;

		SocketImpl() {}

		SocketImpl(
			std::function<int(int, void*, int)> readImpl,
			std::function<int(int, void*, int)> writeImpl,
			std::function<int(int)> closeImpl
		) : 
			read(readImpl), 
			write(writeImpl), 
			close(closeImpl) {}
	};

	class Socket
	{
	public:
		Socket(int fd, SocketImpl& impl);

		Socket(const Socket&) = delete;
		Socket& operator=(const Socket&) = delete;

		~Socket();

        void readInput()
        {
            assert(_peer != nullptr);
            _inputReady = readFromInput();
        }

        void writeOutput()
        {
            assert(_peer != nullptr);
            _outputReady = writeToOutput();
        }

        inline void setPeer(Socket* p)
        {
            _peer = p;
        }

		inline int fd() const { return _fd; }

		void setPoller(Poller* poller)
		{
			_poller = poller;
		}

        bool connected() const { return _connected; }
        void onConnected() { _connected = true; }

        bool closed() const { return _inputClosed && _outputClosed; }

        bool hasPendingIO() const { return (_inputReady || _outputReady) && !closed(); }

    private:
		bool readFromInput();
		bool writeToOutput();

		void onPeerClosed();

		bool read(Buffer& buffer);
		bool send(Buffer& buffer);
        void close();

		void closeInput();

        bool inputClosed() const { return _inputClosed; }
        bool outputClosed() const { return _outputClosed; }
        bool hasQueuedData() const { return !_buffer.consumed(); }

        Buffer& buffer() { return _buffer; }

    private:
		SocketImpl& _impl;
        bool _inputReady = false;
        bool _outputReady = false;
        bool _inputClosed = false;
        bool _outputClosed = false;
        Socket* _peer;
		int _fd;
        bool _connected = false;
		Poller* _poller = nullptr;
        Buffer _buffer;
	};
}