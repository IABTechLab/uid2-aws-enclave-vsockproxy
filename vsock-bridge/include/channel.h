#pragma once

#include <eventdef.h>
#include <socket.h>
#include <logger.h>
#include <threading.h>

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

		Socket* _a;
		Socket* _b;
		ChannelHandle _ha;
		ChannelHandle _hb;
		
		DirectChannel(int id, Socket* a, Socket* b, BlockingQueue<TAction>* taskQueue)
			: _id(id)
			, _a(a)
			, _b(b)
			, _ha(id, a->fd())
			, _hb(id, b->fd())
			, _taskQueue(taskQueue)

		{
			_a->setPeer(b);
			_b->setPeer(a);
		}

		void handle(int fd, int evt)
		{
			
			Socket* s = _a->fd() == fd ? _a : (_b->fd() == fd ? _b : nullptr);
			if (s == nullptr)
			{
				Logger::instance->Log(Logger::WARNING, "error in channel.handle: `id=", _id,"`, `fd=", fd, "` does not belong to this channel");
			}

			if (evt & IOEvent::Error)
			{
				s->incrementEventCount();
				_taskQueue->enqueue(std::bind(&Socket::onError, s));
			}
			else
			{
				if (evt & IOEvent::InputReady)
				{
					s->incrementEventCount();
					_taskQueue->enqueue(std::bind(&Socket::onInputReady, s));
				}

				if (evt & IOEvent::OutputReady)
				{
					s->incrementEventCount();
					_taskQueue->enqueue(std::bind(&Socket::onOutputReady, s));
				}
			}
		}

		bool canBeTerminated() const
		{
			return _a->closed() && _b->closed() && _a->ioEventCount() == 0 && _b->ioEventCount() == 0;
		}
		
		virtual ~DirectChannel()
		{
			delete _a;
			delete _b;
		}
	};
}