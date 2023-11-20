#pragma once

#include <poller.h>

using namespace vsockio;

struct MockPoller : public Poller
{
	struct Fd {
		int fd;
		void* handler;
		uint32_t listeningEvents;
		uint32_t events;
		bool triggerNextTime;

		Fd() : events(IOEvent::None) {}
	};

	bool _inputReady;
	bool _outputReady;
	std::unordered_map<int, Fd> _fdMap;
	bool _triggerNextTime;

	MockPoller(int maxEvents)
	{
		_maxEvents = maxEvents;
	}

	bool add(int fd, void* handler, uint32_t events) override
	{
		Logger::instance->Log(Logger::INFO, "add: ", fd, ",", (uint64_t)handler, ",", events);
		_fdMap[fd].fd = fd;
		_fdMap[fd].handler = handler;
		_fdMap[fd].listeningEvents = events;
		return true;
	}

	bool update(int fd, void* handler, uint32_t events) override
	{
		Logger::instance->Log(Logger::INFO, "update: ", fd, ",", (uint64_t)handler, ",", events);
		_fdMap[fd].fd = fd;
		_fdMap[fd].handler = handler;
		_fdMap[fd].listeningEvents = events;
		return true;
	}

	void remove(int fd) override
	{
		Logger::instance->Log(Logger::INFO, "remove: ", fd);
		_fdMap.erase(fd);
	}

	int poll(VsbEvent* outEvents, int timeout) override
	{
		int numEvents = 0;
		for (auto& fd : _fdMap)
		{
			if (fd.second.triggerNextTime)
			{
				fd.second.triggerNextTime = false;
				outEvents[numEvents].fd = fd.second.fd;
				outEvents[numEvents].data = fd.second.handler;
				outEvents[numEvents].ioFlags = (IOEvent)(fd.second.events & fd.second.listeningEvents);
				numEvents++;
			}
		}
		return numEvents;
	}

	void setInputReady(int fd, bool ready)
	{
		if (ready)
		{
			auto oldEvents = _fdMap[fd].events;
			_fdMap[fd].events |= IOEvent::InputReady;
			if (oldEvents != _fdMap[fd].events)
			{
				_fdMap[fd].triggerNextTime = true;
			}
		}
		else
		{
			_fdMap[fd].events &= ~IOEvent::InputReady;
		}
	}

	void setOutputReady(int fd, bool ready)
	{
		if (ready)
		{
			auto oldEvents = _fdMap[fd].events;
			_fdMap[fd].events |= IOEvent::OutputReady;
			if (oldEvents != _fdMap[fd].events)
			{
				_fdMap[fd].triggerNextTime = true;
			}
		}
		else
		{
			_fdMap[fd].events &= ~IOEvent::OutputReady;
		}
	}
};
