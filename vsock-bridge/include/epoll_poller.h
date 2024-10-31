#pragma once

#include "logger.h"
#include "poller.h"

#include <cstring>
#include <memory>

#include <sys/epoll.h>

namespace vsockio
{
	struct EpollPoller : public Poller
	{
		int _epollFd;

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

		bool add(int fd, void* handler) override
		{
			epoll_event ev;
			memset(&ev, 0, sizeof(epoll_event));
			ev.data.ptr = handler;
			ev.events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP;
			if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, fd, &ev) != 0)
			{
				const int err = errno;
				Logger::instance->Log(Logger::ERROR, "epoll_ctl failed to add fd=", fd, ": ", strerror(err));
				return false;
			}

			return true;
		}

		void remove(int fd) override
		{
			epoll_event ev;
			if (epoll_ctl(_epollFd, EPOLL_CTL_DEL, fd, &ev) != 0)
			{
				const int err = errno;
				Logger::instance->Log(Logger::ERROR, "epoll_ctl failed to delete fd=", fd, ": ", strerror(err));
			}
		}

		int poll(VsbEvent* outEvents, int timeout) override
		{
            PERF_LOG("poll");
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
				if ((_epollEvents[i].events & EPOLLERR) || (_epollEvents[i].events & EPOLLHUP) || (_epollEvents[i].events & EPOLLRDHUP))
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
                outEvents[i].fd = _epollEvents[i].data.fd;
			}

			return eventCount;
		}
	};

    struct EpollPollerFactory : PollerFactory
    {
        int _maxEvents;

        explicit EpollPollerFactory(int maxEvents) : _maxEvents(maxEvents) {}

        std::unique_ptr<Poller> createPoller() override
        {
            return std::make_unique<EpollPoller>(_maxEvents);
        }
    };
}