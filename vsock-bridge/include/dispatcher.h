#pragma once

#include "channel.h"
#include "logger.h"
#include "poller.h"

#include <forward_list>
#include <functional>
#include <unordered_map>
#include <vector>

namespace vsockio
{
	struct ChannelNode
	{
		int _id;
		std::unique_ptr<DirectChannel> _channel;

		explicit ChannelNode(int id) : _id(id) {}

		void reset()
		{
			_channel.reset();
		}

		bool inUse() const
		{
			return !!_channel;
		}
	};

	class ChannelNodePool
	{
	public:
		ChannelNodePool() = default;

		ChannelNodePool(const ChannelNodePool&) = delete;
		ChannelNodePool& operator=(const ChannelNodePool&) = delete;

		~ChannelNodePool()
		{
			for (auto* node : _freeList)
			{
				delete node;
			}
		}

		struct ChannelNodeDeleter
		{
			ChannelNodePool* _pool;

			void operator()(ChannelNode* node)
			{
				_pool->releaseNode(node);
			}
		};

		using ChannelNodePtr = std::unique_ptr<ChannelNode, ChannelNodeDeleter>;

		ChannelNodePtr getFreeNode() {
			const ChannelNodeDeleter deleter{this};

			if (_freeList.empty())
			{
				return ChannelNodePtr(new ChannelNode(_nextNodeId++), deleter);
			}

			auto* node = _freeList.front();
			_freeList.pop_front();
			return ChannelNodePtr(node, deleter);
		}

		void releaseNode(ChannelNode* node)
		{
			if (node == nullptr) return;
			node->reset();
			_freeList.push_front(node);
		}

	private:
		int _nextNodeId = 0;
		std::forward_list<ChannelNode*> _freeList;
	};

	struct Dispatcher
	{
		Poller* _poller;
		std::vector<VsbEvent> _events;
		ChannelNodePool _idman;
		std::unordered_map<uint64_t, ChannelNodePool::ChannelNodePtr> _channels;
		BlockingQueue<std::function<void()>> _tasksToRun;

		int maxNewConnectionPerLoop = 20;
		int scanAndCleanInterval = 20;
		int _currentGen = 0;

		int _name;

		Dispatcher(Poller* poller) : Dispatcher(0, poller) {}

		Dispatcher(int name, Poller* poller) : _name(name), _poller(poller), _events(poller->maxEventsPerPoll()) {}

		int name() const
		{
			return _name;
		}

		void postAddChannel(std::unique_ptr<Socket>&& ap, std::unique_ptr<Socket>(bp))
		{
			// Dispatcher::taskloop manages the channel map attached to the dispatcher
			// connectToPeer modifies the map so we request taskloop thread to run it
			runOnTaskLoop([this, ap = std::move(ap), bp = std::move(bp)]() mutable { addChannel(std::move(ap), std::move(bp)); });
		}
		
		ChannelNode* addChannel(std::unique_ptr<Socket> ap, std::unique_ptr<Socket> bp)
		{
			ChannelNodePool::ChannelNodePtr node = _idman.getFreeNode();
			BlockingQueue<DirectChannel::TAction>* taskQueue = ThreadPool::getTaskQueue(node->_id);

			Logger::instance->Log(Logger::DEBUG, "creating channel id=", node->_id, ", a.fd=", ap->fd(), ", b.fd=", bp->fd());
			node->_channel = std::make_unique<DirectChannel>(node->_id, std::move(ap), std::move(bp), taskQueue);

			const auto& c = *node->_channel;
			c._a->setPoller(_poller);
			c._b->setPoller(_poller);
			if (!_poller->add(c._a->fd(), (void*)&c._ha, IOEvent::InputReady | IOEvent::OutputReady) ||
				!_poller->add(c._b->fd(), (void*)&c._hb, IOEvent::InputReady | IOEvent::OutputReady))
			{
				return nullptr;
			}

			auto* const n = node.get();
			_channels[n->_id] = std::move(node);
			return n;
		}

		template <typename T>
		void runOnTaskLoop(T&& action)
		{
			auto wrapper = std::make_shared<T>(std::forward<T>(action));
			_tasksToRun.enqueue([wrapper] { (*wrapper)(); });
		}

		void run()
		{
			Logger::instance->Log(Logger::DEBUG, "dispatcher ", name(), " started");
			for (;;)
			{
				taskloop();
			}
		}

		void taskloop()
		{
			// handle events on existing channels
			poll();

			// complete new channels
			processQueuedTasks();

			// collect terminated channels
			cleanup();
		}

		void poll()
		{
			const int eventCount = _poller->poll(_events.data(), getTimeout());
			if (eventCount == -1) {
				Logger::instance->Log(Logger::CRITICAL, "Poller returns error.");
				return;
			}

			for (int i = 0; i < eventCount; i++) {
				auto *handle = static_cast<ChannelHandle *>(_events[i].data);
				auto it = _channels.find(handle->channelId);
				if (it == _channels.end() || !it->second->inUse()) {
					Logger::instance->Log(Logger::WARNING, "Channel ID ", handle->channelId, " does not exist.");
					continue;
				}
				auto &channel = *it->second->_channel;
				channel.handle(handle->fd, _events[i].ioFlags);
			}
		}

		void processQueuedTasks()
		{
			for (int i = 0; i < maxNewConnectionPerLoop; i++) {
				// must check task count first, since we don't wanna block here
				if (!_tasksToRun.empty()) {
					auto action = _tasksToRun.dequeue();
					action();
				} else {
					break;
				}
			}
		}

		void cleanup()
		{
			if (_currentGen >= scanAndCleanInterval)
			{
				for (auto it = _channels.begin(); it != _channels.end(); )
				{
					auto* node = it->second.get();
					if (!node->inUse() || node->_channel->canBeTerminated())
					{
						Logger::instance->Log(Logger::DEBUG, "destroying channel id=", it->first);

						// any resources allocated on channel thread must be freed there
						if (node->inUse())
						{
							node->_channel.release()->terminate();
						}

						it = _channels.erase(it);
					}
					else
					{
						++it;
					}
				}
				_currentGen = 0;
			}
			_currentGen++;
		}

		int getTimeout() const
		{
			const bool hasPendingTask =
				(_currentGen >= scanAndCleanInterval) ||
				(!_tasksToRun.empty());

			return hasPendingTask ? 0 : 16;
		}
	};
}