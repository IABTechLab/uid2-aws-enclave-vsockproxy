#pragma once

#include "eventdef.h"
#include "logger.h"
#include "socket.h"
#include "threading.h"

#include <memory>

namespace vsockio
{
	struct ChannelHandle
	{
		int channelId;
		int fd;

		ChannelHandle(int channelId, int fd)
			: channelId(channelId), fd(fd) {}
	};

	struct DirectChannel
	{
		using TAction = std::function<void()>;

		int _id;
		BlockingQueue<TAction>* _taskQueue;

		std::unique_ptr<Socket> _a;
		std::unique_ptr<Socket> _b;
		ChannelHandle _ha;
		ChannelHandle _hb;
		
		DirectChannel(int id, std::unique_ptr<Socket> a, std::unique_ptr<Socket> b, BlockingQueue<TAction>* taskQueue)
			: _id(id)
			, _a(std::move(a))
			, _b(std::move(b))
			, _ha(id, _a->fd())
			, _hb(id, _b->fd())
			, _taskQueue(taskQueue)

		{
			_a->setPeer(_b.get());
			_b->setPeer(_a.get());
		}

		void handle(int fd, int evt)
		{
			Socket* s = _a->fd() == fd ? _a.get() : (_b->fd() == fd ? _b.get() : nullptr);
			if (s == nullptr)
			{
				Logger::instance->Log(Logger::WARNING, "error in channel.handle: `id=", _id,"`, `fd=", fd, "` does not belong to this channel");
				return;
			}

			if (evt & IOEvent::Error)
			{
				Logger::instance->Log(Logger::DEBUG, "poll error for fd=", fd);
				evt |= IOEvent::InputReady;
				evt |= IOEvent::OutputReady;
			}

			if (evt & IOEvent::InputReady)
			{
				s->onIoEvent();
				_taskQueue->enqueue([=] { s->onInputReady(); });
			}

			if (evt & IOEvent::OutputReady)
			{
				s->onIoEvent();
				_taskQueue->enqueue([=] { s->onOutputReady(); });
			}
		}

		bool canBeTerminated() const
		{
			return _a->closed() && _b->closed() && _a->ioEventCount() == 0 && _b->ioEventCount() == 0;
		}
	};
}