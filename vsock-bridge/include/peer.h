#pragma once

#include <atomic>
#include <list>
#include <memory>
#include <linux/socket.h>
#include <vector>

#include <buffer.h>

namespace vsockio
{
	template <typename T>
	struct UniquePtrQueue
	{
		using TPtr = std::unique_ptr<T>;

		std::list<TPtr> _list;

		int _count;

		UniquePtrQueue() : _count(0) {}

		ssize_t count() const
		{
			return _count;
		}

		TPtr& front()
		{
			return _list.front();
		}

		void enqueue(TPtr& value)
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
	struct Peer
	{
		Peer(int inputState, int outputState) 
			: _inputReady(inputState), _outputReady(outputState)
			, _inputClosed(false), _outputClosed(false)
			, _queueFull(false), _peer(nullptr), _ioEventCount(0) {}

		void onInputReady()
		{
			_inputReady = true;
			bool continuation = false;
			do
			{
				readFromInput(continuation);
				if (continuation)
				{
					_peer->writeToOutput(continuation);
				}
			} while (continuation);
			_ioEventCount--;
		}

		void onOutputReady()
		{
			_outputReady = true;
			bool continuation = false;
			do
			{
				writeToOutput(continuation);
				if (continuation)
				{
					_peer->readFromInput(continuation);
				}
			} while (continuation);
			_ioEventCount--;
		}

		void onError()
		{
			shutdown();
			_ioEventCount--;
		}

		virtual void shutdown() = 0;

		virtual void onPeerShutdown() = 0;

		virtual void readFromInput(bool& continuation) = 0;

		virtual void writeToOutput(bool& continuation) = 0;

		virtual void queue(TBuf& buffer) = 0;

		bool inputClosed() const { return _inputClosed; }

		bool outputClosed() const { return _outputClosed; }

		bool closed() const { return _inputClosed && _outputClosed; }

		bool queueFull() const { return _queueFull; }

		virtual ~Peer() {}

		inline void setPeer(Peer* p) { _peer = p; }

		inline void incrementEventCount() { _ioEventCount++; }

		inline int ioEventCount() const { return _ioEventCount.load(); }

	protected:
		bool _inputReady;
		bool _outputReady;
		int _inputClosed;
		int _outputClosed;
		bool _queueFull;
		Peer<TBuf>* _peer;

	private:
		std::atomic_int _ioEventCount;
	};
}