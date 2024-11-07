#pragma once

#include <condition_variable>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <vector>

namespace vsockio
{
    template <typename T>
    struct ThreadSafeQueue
    {
        std::queue<T> _queue;
        std::mutex _queueLock;

        void enqueue(T&& value)
        {
            std::lock_guard<std::mutex> lk(_queueLock);
            _queue.push(std::move(value));
        }

        std::optional<T> dequeue()
        {
            std::lock_guard<std::mutex> lk(_queueLock);
            if (_queue.empty())
            {
                return std::nullopt;
            }

            T result = std::move(_queue.front());
            _queue.pop();
            return result;
        }
    };

}