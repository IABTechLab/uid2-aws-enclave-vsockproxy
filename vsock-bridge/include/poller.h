#pragma once

#include "eventdef.h"

#include <memory>

namespace vsockio
{
	struct Poller
	{
        virtual ~Poller() = default;

		virtual bool add(int fd, void* handler) = 0;

		virtual void remove(int fd) = 0;

		virtual int poll(VsbEvent* outEvents, int timeout) = 0;

		int maxEventsPerPoll() const { return _maxEvents; }

    protected:
		int _maxEvents;
	};

    struct PollerFactory
    {
        virtual ~PollerFactory() = default;

        virtual std::unique_ptr<Poller> createPoller() = 0;
    };
}