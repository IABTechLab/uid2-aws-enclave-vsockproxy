#pragma once

#include <vector>
#include <functional>
#include <poller.h>
#include <unordered_map>
#include <channel.h>
#include <logger.h>

namespace vsockio
{
	struct ChannelIdListNode
	{
		int id;
		bool inUse;
		DirectChannel* channel;
		ChannelIdListNode* next;
		ChannelIdListNode* prev;

		ChannelIdListNode(int id) 
			: id(id), inUse(false), channel(nullptr), next(nullptr), prev(nullptr) {}
	};

	struct ChannelIdList
	{
		// [head][free(p)][free]...[free][tail]

		ChannelIdListNode* head;
		ChannelIdListNode* p;
		ChannelIdListNode* tail;
		uint32_t _nextId;

		ChannelIdList() : head(nullptr), tail(nullptr), _nextId(0)
		{
			head = new ChannelIdListNode(0);
			tail = new ChannelIdListNode(0);
			head->prev = nullptr;
			head->next = tail;
			tail->prev = head;
			tail->next = nullptr;
			p = tail;
		}

		ChannelIdListNode* getNode()
		{
			if (p == head || p == tail)
			{
				// no free node because we do not have any or we used all up
				putNode(new ChannelIdListNode(_nextId++));
			}

			auto* prev = p->prev;
			auto* next = p->next;
			next->prev = prev;
			prev->next = next;
			
			auto* ret = p;
			p = next;
			ret->inUse = true;
			return ret;
		}

		void putNode(ChannelIdListNode* node)
		{
			node->inUse = false;
			node->channel = nullptr;
			node->next = tail;
			node->prev = tail->prev;
			tail->prev->next = node;
			tail->prev = node;

			if (p == tail)
			{
				p = node;
			}
		}

		~ChannelIdList()
		{
			auto* ptr = head;
			while (ptr != nullptr)
			{
				auto* x = ptr;
				ptr = ptr->next;
				delete x;
			}
		}
	};

	struct Dispatcher
	{
		Poller* _poller;
		VsbEvent* _events;
		std::unordered_map<uint64_t, ChannelIdListNode*> _channels;
		ChannelIdList _idman;
		BlockingQueue<std::function<void()>> _tasksToRun;

		int maxNewConnectionPerLoop = 20;
		int scanAndCleanInterval = 20;
		uint64_t _lastScanAndCleanGen;
		uint64_t _currentGen;

		int _name;

		Dispatcher(Poller* poller) : Dispatcher(0, poller) {}

		Dispatcher(int name, Poller* poller) : _name(name), _poller(poller), _events(new VsbEvent[poller->maxEventsPerPoll()]) {}
		
		ChannelIdListNode* prepareChannel()
		{
			return _idman.getNode();
		}

		void makeChannel(ChannelIdListNode* node, Socket* a, Socket* b, BlockingQueue<DirectChannel::TAction>* _taskQueue)
		{
			Logger::instance->Log(Logger::DEBUG, "creating channel id=", node->id, ", a.fd=", a->fd(), ", b.fd=", b->fd());
			auto* c = new DirectChannel(node->id, a, b, _taskQueue);
			_channels[node->id] = node;
			node->channel = c;
			_poller->add(a->fd(), (void*)&c->_ha, IOEvent::InputReady | IOEvent::OutputReady);
			_poller->add(b->fd(), (void*)&c->_hb, IOEvent::InputReady | IOEvent::OutputReady);
		}

		void destroyChannel(ChannelIdListNode* node)
		{
			Logger::instance->Log(Logger::DEBUG, "destroying channel id=", node->channel->_id);
			_idman.putNode(node);
			delete node->channel;
		}

		void runOnTaskLoop(std::function<void()> action)
		{
			_tasksToRun.enqueue(action);
		}

		void run()
		{
			Logger::instance->Log(Logger::DEBUG, "dispatcher started");
			for (;;)
			{
				taskloop();
			}
		}

		void taskloop()
		{
			// Phase 1. poll IO events
			int eventCount = _poller->poll(_events, getTimeout());
			if (eventCount == -1)
			{
				Logger::instance->Log(Logger::CRITICAL, "Poller returns error.");
				return;
			}

			// Phase 2. find corresponding handler and process events
			for (int i = 0; i < eventCount; i++)
			{
				auto* handle = static_cast<ChannelHandle*>(_events[i].data);
				auto it = _channels.find(handle->channelId);
				if (it == _channels.end() || !it->second->inUse || it->second->channel == nullptr)
				{
					Logger::instance->Log(Logger::WARNING, "Channel ID ", handle->channelId, " does not exist.");
					continue;
				}
				auto* channel = it->second->channel;
				channel->handle(handle->fd, _events[i].ioFlags);
			}

			// Phase 3. complete newcoming connections
			for (int i = 0; i < maxNewConnectionPerLoop; i++)
			{
				// must check task count first, since we don't wanna block here
				if (_tasksToRun.count() > 0)
				{
					auto action = _tasksToRun.dequeue();
					action();
				}
				else
				{
					break;
				}
			}

			// Phase 4. clean & remove terminated channels
			if (_currentGen - _lastScanAndCleanGen == scanAndCleanInterval)
			{
				std::vector<int> keysToRemove;
				for (auto it = _channels.begin(); it != _channels.end(); it++)
				{
					auto* ch = it->second->channel;
					if (ch != nullptr && ch->canBeTerminated())
					{
						keysToRemove.push_back(it->first);
						destroyChannel(it->second);
					}
				}
				for (int id : keysToRemove)
				{
					_channels.erase(id);
				}
				_lastScanAndCleanGen = _currentGen;
			}
			_currentGen++;
		}

		int getTimeout() const
		{
			bool hasPendingTask =
				(_currentGen - _lastScanAndCleanGen == scanAndCleanInterval) ||
				(_tasksToRun.count() > 0);

			return hasPendingTask ? 0 : 16;
		}
	};
}