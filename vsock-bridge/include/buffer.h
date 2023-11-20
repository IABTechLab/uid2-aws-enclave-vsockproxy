#pragma once

#include "logger.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <list>
#include <memory>
#include <vector>

#include <unistd.h>

namespace vsockio
{
	struct MemoryBlock
	{
		MemoryBlock(int size, class MemoryArena* region)
			: _startPtr(new uint8_t[size]), _region(region) {}

		uint8_t* offset(int x) const
		{
			return _startPtr.get() + x;
		}

		std::unique_ptr<uint8_t[]> _startPtr;
		class MemoryArena* _region;
	};
	
	struct MemoryArena
	{
		std::vector<MemoryBlock> _blocks;
		std::list<MemoryBlock*> _handles;
		uint32_t _blockSizeInBytes = 0;
		bool _initialized = false;

		MemoryArena() = default;

		void init(int blockSize, int numBlocks)
		{
			if (_initialized) throw;

			Logger::instance->Log(Logger::INFO, "Thread-local memory arena init: blockSize=", blockSize, ", numBlocks=", numBlocks);

			_blockSizeInBytes = blockSize;

			for (int i = 0; i < numBlocks; i++)
			{
				_blocks.emplace_back(blockSize, this);
			}

			for (int i = 0; i < numBlocks; i++)
			{
				_handles.push_back(&_blocks[i]);
			}
			
			_initialized = true;
		}

		MemoryBlock* get()
		{
			if (!_handles.empty())
			{
				auto mb = _handles.front();
				_handles.pop_front();
				return mb;
			}
			else
			{
				return new MemoryBlock(_blockSizeInBytes, nullptr);
			}
		}

		void put(MemoryBlock* mb) 
		{
			if (mb->_region == this)
			{
				_handles.push_front(mb);
			}
			else if (mb->_region == nullptr)
			{
				delete mb;
			}
			else
			{
				throw;
			}
		}

		int blockSize() const { return _blockSizeInBytes; }
	};

	struct Buffer
	{
		constexpr static int MAX_PAGES = 20;
		int _pageCount;
		int _cursor;
		int _size;
		int _pageSize;
		MemoryBlock* _pages[MAX_PAGES];
		MemoryArena* _arena;

		explicit Buffer(MemoryArena* arena) : _arena(arena), _pageCount{ 0 }, _cursor{ 0 }, _size{ 0 }, _pageSize(arena->blockSize()) {}

		Buffer(Buffer&& b) : _arena(b._arena), _pageCount(b._pageCount), _cursor(b._cursor), _size(b._size), _pageSize(b._arena->blockSize())
		{
			for (int i = 0; i < _pageCount; i++)
			{
				_pages[i] = b._pages[i];
			}
			b._pageCount = 0; // prevent _pages being destructed by old object
		}

		Buffer(const Buffer&) = delete;
		Buffer& operator=(const Buffer&) = delete;

		~Buffer()
		{
			for (int i = 0; i < _pageCount; i++)
			{
				_arena->put(_pages[i]);
			}
		}

		uint8_t* tail() const
		{
			return offset(_size);
		}

		int remainingCapacity() const
		{
			return capacity() - _size;
		}

		void produce(int size)
		{
			_size += size;
		}

		bool ensureCapacity()
		{
			return remainingCapacity() > 0 || tryNewPage();
		}

		uint8_t* head() const
		{
			return offset(_cursor);
		}

		int headLimit() const
		{
			return std::min(pageLimit(_cursor), _size - _cursor);
		}

		void consume(int size)
		{
			_cursor += size;
		}

		bool tryNewPage()
		{
			if (_pageCount >= MAX_PAGES) return false;
			_pages[_pageCount++] = _arena->get();
			return true;
		}

		uint8_t* offset(int x) const
		{
			return _pages[x / _pageSize]->offset(x % _pageSize);
		}

		int capacity() const
		{
			return _pageCount * _pageSize;
		}

		int pageLimit(int x) const
		{
			return _pageSize - (x % _pageSize);
		}

		int cursor() const
		{
			return _cursor;
		}

		int size() const
		{
			return _size;
		}

		bool empty() const
		{
			return _size <= 0;
		}

		bool consumed() const
		{
			return _cursor >= _size;
		}
	};

	struct BufferManager
	{
		thread_local static MemoryArena* arena;

		static std::unique_ptr<Buffer> getBuffer()
		{
			auto b = std::make_unique<Buffer>(arena);
			b->tryNewPage();
			return b;
		}

		static std::unique_ptr<Buffer> getEmptyBuffer()
		{
			return std::make_unique<Buffer>(arena);
		}
	};


}