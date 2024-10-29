#pragma once

#include "peer.h"
#include "poller.h"

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

	class Socket : public Peer<std::unique_ptr<Buffer>>
	{
	public:
		Socket(int fd, SocketImpl& impl);

		Socket(const Socket&) = delete;
		Socket& operator=(const Socket&) = delete;

		~Socket();

		inline int fd() const { return _fd; }

		void close() override;

		bool queueEmpty() const override { return _sendQueue.empty(); }

		void setPoller(Poller* poller)
		{
			_poller = poller;
		}

	protected:
		bool readFromInput() override;

		bool writeToOutput() override;

		void onPeerClosed() override;

		void queue(std::unique_ptr<Buffer>&& buffer) override;

	private:
		std::unique_ptr<Buffer> read();

		bool send(Buffer& buffer);

		void closeInput();

	private:
		SocketImpl& _impl;
		UniquePtrQueue<Buffer> _sendQueue;
		int _fd;
		Poller* _poller = nullptr;
	};
}