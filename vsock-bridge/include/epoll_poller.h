#pragma once

#include <poller.h>
#include <sys/epoll.h>
#include <cstring>
#include <logger.h>

namespace vsockio
{
	struct EpollPoller : public Poller
	{
		int _epollFd;

		uint64_t _pollCounter;
		std::unique_ptr<epoll_event[]> _epollEvents;

		EpollPoller(int maxEvents) : _epollEvents(new epoll_event[maxEvents])
		{
			_maxEvents = maxEvents;
			_epollFd = epoll_create1(0);
			if (_epollFd == -1)
			{
				Logger::instance->Log(Logger::CRITICAL, "epoll_create1 failed: ", strerror(errno));
				return;
			}
		}

		int add(int fd, void* handler, uint32_t events) override
		{
			epoll_event ev;
			memset(&ev, 0, sizeof(epoll_event));
			ev.data.ptr = handler;
			ev.events = vsb2epoll(events);
			return epoll_ctl(_epollFd, EPOLL_CTL_ADD, fd, &ev);
		}

		int update(int fd, void* handler, uint32_t events) override
		{
			epoll_event ev;
			memset(&ev, 0, sizeof(epoll_event));
			ev.data.ptr = handler;
			ev.events = vsb2epoll(events);
			return epoll_ctl(_epollFd, EPOLL_CTL_MOD, fd, &ev);
		}

		void remove(int fd) override
		{
			epoll_event ev;
			epoll_ctl(_epollFd, EPOLL_CTL_DEL, fd, &ev);
		}

		int poll(VsbEvent* outEvents, int timeout) override
		{
			int eventCount = epoll_wait(_epollFd, _epollEvents.get(), _maxEvents, timeout);

			if (eventCount == -1)
			{
				int err = errno;
				Logger::instance->Log(Logger::ERROR, "epoll_wait returns error code ", err, ": ", strerror(err));
				// clean up
			}

			for (int i = 0; i < eventCount; i++)
			{
				// Design:
				// we parse epoll event and translate to application defined events
				// and leave the list of events to main processing thread

				outEvents[i].ioFlags = IOEvent::None;
				if ((_epollEvents[i].events & EPOLLERR) || (_epollEvents[i].events & EPOLLHUP))
				{
					outEvents[i].ioFlags = static_cast<IOEvent>(outEvents[i].ioFlags | IOEvent::Error);
				}
				else
				{
					if (_epollEvents[i].events & EPOLLIN)
						outEvents[i].ioFlags = static_cast<IOEvent>(outEvents[i].ioFlags | IOEvent::InputReady);

					if (_epollEvents[i].events & EPOLLOUT)
						outEvents[i].ioFlags = static_cast<IOEvent>(outEvents[i].ioFlags | IOEvent::OutputReady);
				}

				outEvents[i].data = _epollEvents[i].data.ptr;
			}

			return eventCount;
		}

		inline uint32_t vsb2epoll(uint32_t vsbEvent) const
		{
			uint32_t evts = EPOLLET;

			if (vsbEvent & IOEvent::InputReady)
			{
				evts = evts | EPOLLIN;
			}

			if (vsbEvent & IOEvent::OutputReady)
			{
				evts = evts | EPOLLOUT;
			}

			return evts;
		}
	};
}