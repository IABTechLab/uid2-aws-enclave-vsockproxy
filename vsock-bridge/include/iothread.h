#pragma once

#include "channel.h"
#include "poller.h"
#include "socket.h"
#include "threading.h"

#include <atomic>
#include <functional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vsockio
{
    class IOThread
    {
    public:
        explicit IOThread(size_t threadId, PollerFactory& pollerFactory)
            : _id(threadId)
            , _poller(pollerFactory.createPoller())
            , _events(_poller->maxEventsPerPoll())
            , _thr([this] { run(); })
        {
        }

        ~IOThread()
        {
            _terminateFlag = true;

            if (_thr.joinable())
            {
                _thr.join();
            }
        }

        size_t id() const { return _id; }

        void addChannel(std::unique_ptr<Socket>&& ap, std::unique_ptr<Socket>&& bp);
        void terminateChannel(DirectChannel* channel);

    private:
        struct PendingChannel
        {
            std::unique_ptr<Socket> _ap;
            std::unique_ptr<Socket> _bp;
        };

        void run();
        void addPendingChannels();
        void addPendingChannel(PendingChannel&& pendingChannel);
        void poll();
        int getPollTimeout() const;
        void performIO();
        void cleanup();

        const size_t _id;
        std::atomic<bool> _terminateFlag = false;
        std::unique_ptr<Poller> _poller;
        ThreadSafeQueue<PendingChannel> _pendingChannels;
        std::unordered_set<DirectChannel*> _channels;
        std::unordered_set<DirectChannel*> _readyChannels;
        std::unordered_set<DirectChannel*> _terminatedChannels;
        std::vector<VsbEvent> _events;
        std::thread _thr;
    };

    class IOThreadPool
    {
    public:
        explicit IOThreadPool(size_t size, PollerFactory& pollerFactory)
        {
            for (size_t i = 0; i < size; ++i) {
                _threads.push_back(std::make_unique<IOThread>(i, pollerFactory));
            }
        }

        void addChannel(std::unique_ptr<Socket>&& ap, std::unique_ptr<Socket>&& bp) const
        {
            thread_local static size_t channelCount = 0;
            _threads[channelCount % _threads.size()]->addChannel(std::move(ap), std::move(bp));
            ++channelCount;
        }

    private:
        std::vector<std::unique_ptr<IOThread>> _threads;
    };

}
