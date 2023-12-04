#pragma once

#include <condition_variable>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace vsockio
{
	template <typename T>
	struct BlockingQueue
	{
		std::list<T> _list;
		std::mutex _queueLock;
		std::condition_variable _signal;
		int _count;

		BlockingQueue() : _count(0) {}

		void enqueue(T value)
		{
			{
				std::lock_guard<std::mutex> lk(_queueLock);
				_list.push_back(std::move(value));
				++_count;
			}
			_signal.notify_one();
		}

		T dequeue()
		{
			std::unique_lock<std::mutex> lk(_queueLock);
			if (_count == 0)
			{
				_signal.wait(lk, [this]() { return _count > 0; });
			}
			
			T p = _list.front();
			_list.pop_front();
			--_count;

			lk.unlock();
			return p;
		}

		int count() const
		{
			return _count;
		}

		bool empty() const
		{
			return _count <= 0;
		}
	};

	struct WorkerThread
	{
		std::function<void()> _initCallback;
		BlockingQueue<std::function<void()>> _taskQueue;
		bool _retired = false;
		uint64_t _eventsProcessed = 0;
		std::thread t;

		uint64_t eventsProcessed() const { return _eventsProcessed; }

		WorkerThread(std::function<void()> initCallback) 
			: _initCallback(initCallback), t([this] { run(); })
		{
		}

		~WorkerThread()
		{
			if (t.joinable())
			{
				t.join();
			}
		}

		void run()
		{
			_initCallback();

			while (!_retired)
			{
				auto action = _taskQueue.dequeue();
				action();
				_eventsProcessed++;
			}
		}

		void stop()
		{
			_retired = true;
			_taskQueue.enqueue([](){});
		}

		BlockingQueue<std::function<void()>>* getQueue()
		{
			return &_taskQueue;
		}
	};

	struct ThreadPool
	{
		static std::vector<std::unique_ptr<WorkerThread>> threads;
		static BlockingQueue<std::function<void()>>* getTaskQueue(int taskId)
		{
			return threads[taskId % ThreadPool::threads.size()]->getQueue();
		}
	};

}