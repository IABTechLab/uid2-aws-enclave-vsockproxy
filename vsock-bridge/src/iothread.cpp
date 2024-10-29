#include <iothread.h>

namespace vsockio
{
    void IOThread::addChannel(std::unique_ptr<Socket>&& ap, std::unique_ptr<Socket>&& bp)
    {
        _pendingChannels.enqueue({std::move(ap), std::move(bp)});
    }

    void IOThread::run()
    {
        while (!_terminateFlag.load(std::memory_order_relaxed))
        {
            addPendingChannels();
            poll();
            performIO();
            cleanup();
        }
    }

    void IOThread::addPendingChannels()
    {
        while (true)
        {
            auto pendingChannel = _pendingChannels.dequeue();
            if (!pendingChannel)
            {
                break;
            }

            addPendingChannel(std::move(*pendingChannel));
        }
    }

    void IOThread::addPendingChannel(PendingChannel&& pendingChannel)
    {
        thread_local static int channelId = 0;

        Logger::instance->Log(Logger::DEBUG, "iothread id=", id(), " creating channel id=", channelId, ", a.fd=", pendingChannel._ap->fd(), ", b.fd=", pendingChannel._bp->fd());
        auto channel = std::make_unique<DirectChannel>(channelId, std::move(pendingChannel._ap), std::move(pendingChannel._bp));
        ++channelId;

        channel->_a->setPoller(_poller.get());
        channel->_b->setPoller(_poller.get());
        if (!_poller->add(channel->_a->fd(), (void*)&channel->_ha) ||
            !_poller->add(channel->_b->fd(), (void*)&channel->_hb))
        {
            return;
        }

        _channels.insert(channel.release());
    }

    void IOThread::poll()
    {
        const int eventCount = _poller->poll(_events.data(), getPollTimeout());
        if (eventCount == -1) {
            Logger::instance->Log(Logger::CRITICAL, "Poller returns error.");
            return;
        }

        for (int i = 0; i < eventCount; i++) {
            auto* handle = static_cast<ChannelHandle *>(_events[i].data);
            auto* channel = handle->_channel;
            _readyChannels.insert(channel);

            Socket& s = channel->getSocket(handle->_fd);
            if ((_events[i].ioFlags & (IOEvent::OutputReady | IOEvent::Error)) && !s.connected())
            {
                s.checkConnected();
            }
        }
    }

    int IOThread::getPollTimeout() const
    {
        return _readyChannels.empty() ? 1 : 0;
    }

    void IOThread::performIO()
    {
        for (auto it = _readyChannels.begin(); it != _readyChannels.end(); )
        {
            auto* channel = *it;
            channel->performIO();
            if (!channel->canReadWriteMore())
            {
                it = _readyChannels.erase(it);
            }
            else
            {
                ++it;
            }

            if (channel->canBeTerminated())
            {
                _terminatedChannels.insert(channel);
            }
        }
    }

    void IOThread::cleanup()
    {
        if (_terminatedChannels.empty())
        {
            return;
        }

        for (auto* channel : _terminatedChannels)
        {
            _channels.erase(channel);
            _readyChannels.erase(channel);
            delete channel;
        }

        _terminatedChannels.clear();
    }
}
