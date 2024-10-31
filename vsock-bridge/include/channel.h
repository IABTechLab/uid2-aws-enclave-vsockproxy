#pragma once

#include "eventdef.h"
#include "logger.h"
#include "socket.h"
#include "threading.h"

#include <forward_list>
#include <memory>

namespace vsockio
{
    struct DirectChannel;
    class IOThread;

	struct ChannelHandle
	{
        DirectChannel* _channel;
        int _id;
		int _fd;

		ChannelHandle(DirectChannel* channel, int id, int fd)
			: _channel(channel), _id(id), _fd(fd) {}
	};

	struct DirectChannel
	{
		using TAction = std::function<void()>;

		int _id;
		IOThread& _ioThread;

		std::unique_ptr<Socket> _a;
		std::unique_ptr<Socket> _b;
		ChannelHandle _ha;
		ChannelHandle _hb;
		
		DirectChannel(int id, std::unique_ptr<Socket> a, std::unique_ptr<Socket> b, IOThread& ioThread)
			: _id(id)
            , _ioThread(ioThread)
			, _a(std::move(a))
			, _b(std::move(b))
			, _ha(this, _id, _a->fd())
			, _hb(this, _id, _b->fd())

		{
			_a->setPeer(_b.get());
			_b->setPeer(_a.get());
		}

        void performIO();

        bool hasPendingIO() const
        {
            return _a->hasPendingIO() || _b->hasPendingIO();
        }

		bool canBeTerminated() const
		{
			return _a->closed() && _b->closed();
		}

        Socket& getSocket(int fd) const
        {
            if (fd == _a->fd()) return *_a;
            if (fd == _b->fd()) return *_b;
            throw std::runtime_error("unexpected fd for channel");
        }
	};
}