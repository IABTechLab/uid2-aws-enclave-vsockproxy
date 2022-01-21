#pragma once

#include <functional>
#include <peer.h>

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

	struct Socket : public Peer<std::unique_ptr<Buffer>>
	{
		void readFromInput(bool& continuation) override;

		void writeToOutput(bool& continuation) override;

		void shutdown() override;

		void onPeerShutdown() override;

		void queue(std::unique_ptr<Buffer>& buffer) override;

		std::unique_ptr<Buffer> read();

		void send(std::unique_ptr<Buffer>& buffer);

		void closeInput();

		void closeOutput();

		inline int fd() const { return _fd; }

		Socket(int fd, SocketImpl* impl);

		Socket(const Socket& _) = delete;

		~Socket();

	private:
		SocketImpl* _impl;
		UniquePtrQueue<Buffer> _sendQueue;
		int _fd;
	};
}