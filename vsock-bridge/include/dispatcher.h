#pragma once

#include "iothread.h"

namespace vsockio
{

    class Dispatcher
    {
    public:
        explicit Dispatcher(const IOThreadPool& threadPool) : _threadPool(threadPool) {}

        void addChannel(std::unique_ptr<Socket>&& ap, std::unique_ptr<Socket>&& bp)
        {
            _threadPool.addChannel(std::move(ap), std::move(bp));
        }

    private:
        const IOThreadPool& _threadPool;
    };

}