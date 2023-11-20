#pragma once

#include "buffer.h"

#include <atomic>
#include <cassert>
#include <list>
#include <memory>
#include <vector>

#include <linux/socket.h>

namespace vsockio
{
	template <typename T>
	struct UniquePtrQueue
	{
		using TPtr = std::unique_ptr<T>;

		std::list<TPtr> _list;

		ssize_t _count;

		UniquePtrQueue() : _count(0) {}

		ssize_t count() const
		{
			return _count;
		}

		TPtr& front()
		{
			return _list.front();
		}

		void enqueue(TPtr&& value)
		{
			_count++;
			_list.push_back(std::move(value));
		}

		TPtr dequeue()
		{
			_count--;
			TPtr p = std::move(_list.front());
			_list.pop_front();
			return p;
		}

		bool empty() const
		{
			return _list.empty();
		}
	};

	template <typename TBuf>
	class Peer
	{
	public:
		Peer() = default;

		Peer(const Peer&) = delete;
		Peer& operator=(const Peer&) = delete;

		virtual ~Peer() {}

		void onInputReady()
		{
			assert(_peer != nullptr);

			_inputReady = true;
			while (readFromInput() && _peer->writeToOutput())
				;
			--_ioEventCount;
		}

		void onOutputReady()
		{
			assert(_peer != nullptr);

			_outputReady = true;
			while (writeToOutput() && _peer->readFromInput())
				;
			--_ioEventCount;
		}

		void onError()
		{
			handleError();
			--_ioEventCount;
		}

		inline void setPeer(Peer* p)
		{
			_peer = p;
		}

		inline void onIoEvent() { ++_ioEventCount; }

		inline int ioEventCount() const { return _ioEventCount.load(); }

		virtual void close() = 0;

		bool closed() const { return _inputClosed && _outputClosed; }

		bool inputClosed() const { return _inputClosed; }

		bool outputClosed() const { return _outputClosed; }

		virtual void onPeerClosed() = 0;

		virtual void queue(TBuf&& buffer) = 0;

		bool queueFull() const { return _queueFull; }
		virtual bool queueEmpty() const = 0;

	protected:
		virtual bool readFromInput() = 0;

		virtual bool writeToOutput() = 0;

		virtual void handleError() = 0;

		virtual int name() const = 0;

	protected:
		bool _inputReady = false;
		bool _outputReady = false;
		bool _inputClosed = false;
		bool _outputClosed = false;
		bool _queueFull = false;
		Peer<TBuf>* _peer = nullptr;

	private:
		std::atomic_int _ioEventCount{0};
	};
}