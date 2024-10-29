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

        void readInput()
        {
            assert(_peer != nullptr);
            _inputReady = readFromInput();
        }

        void writeOutput()
        {
            assert(_peer != nullptr);
            _outputReady = writeToOutput();
        }

		inline void setPeer(Peer* p)
		{
			_peer = p;
		}

		virtual void close() = 0;

		bool closed() const { return _inputClosed && _outputClosed; }

		bool inputClosed() const { return _inputClosed; }

		bool outputClosed() const { return _outputClosed; }

		virtual void onPeerClosed() = 0;

		virtual void queue(TBuf&& buffer) = 0;

		bool queueFull() const { return _queueFull; }
		virtual bool queueEmpty() const = 0;

        bool hasPendingIO() const { return _inputReady && !closed(); }

	protected:
		virtual bool readFromInput() = 0;

		virtual bool writeToOutput() = 0;

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