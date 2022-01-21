#pragma once

#include <unistd.h>
#include <stdint.h>
#include <iostream>
#include <vector>
#include <list>
#include <logger.h>

namespace vsockio
{
	struct MemoryBlock
	{
		MemoryBlock(uint8_t* startPtr, class MemoryArena* region)
			: _startPtr(startPtr), _region(region) {}

		MemoryBlock(MemoryBlock&& other) : _startPtr(other._startPtr), _region(other._region) {}

		MemoryBlock(const MemoryBlock& other) = delete;

		uint8_t* offset(int x) 
		{
			return _startPtr + x;
		}

		uint8_t* _startPtr;
		class MemoryArena* _region;
	};
	
	struct MemoryArena
	{
		std::vector<MemoryBlock> _blocks;
		std::list<MemoryBlock*> _handles;
		uint8_t* _memoryStart;
		uint32_t _blockSizeInBytes;
		int _numBlocks;
		bool _initialized;

		MemoryArena() 
			: _initialized(false), _numBlocks(0), _memoryStart(nullptr), _blocks{} {}

		void init(uint32_t blockSize, int numBlocks)
		{
			if (_initialized) throw;

			Logger::instance->Log(Logger::INFO, "Thread-local memory arena init: blockSize=", blockSize, ", numBlocks=", numBlocks);

			_numBlocks = numBlocks;
			_blockSizeInBytes = blockSize;
			_memoryStart = static_cast<uint8_t*>(malloc(blockSize * numBlocks * sizeof(uint8_t)));

			for (int i = 0; i < numBlocks; i++)
			{
				_blocks.emplace_back(MemoryBlock( startPtrOf(i), this ));
			}

			for (int i = 0; i < numBlocks; i++)
			{
				_handles.push_back(&_blocks[i]);
			}
			
			_initialized = true;
		}

		inline uint8_t* startPtrOf(int blockIndex) const
		{
			return _memoryStart + (blockIndex * _blockSizeInBytes);
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
				return new MemoryBlock(new uint8_t[_blockSizeInBytes], nullptr);
			}
		}

		void put(MemoryBlock* mb) 
		{
			if (mb->_region == this)
			{
				_handles.push_back(mb);
			}
			else if (mb->_region == nullptr)
			{
				delete[] mb->_startPtr;
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

		Buffer(MemoryArena* arena) : _arena(arena), _pageCount{ 0 }, _cursor{ 0 }, _size{ 0 }, _pageSize(arena->blockSize()) {}

		Buffer(Buffer&& b) : _arena(b._arena), _pageCount(b._pageCount), _cursor(b._cursor), _size(b._size), _pageSize(b._arena->blockSize())
		{
			for (int i = 0; i < _pageCount; i++)
			{
				_pages[i] = b._pages[i];
			}
			b._pageCount = 0; // prevent _pages being destructed by old object
		}

		Buffer(const Buffer& _) = delete;

		bool tryNewPage()
		{
			if (_pageCount >= MAX_PAGES) return false;
			_pages[_pageCount++] = _arena->get();
			return true;
		}

		uint8_t* offset(ssize_t x)
		{
			return _pages[x / _pageSize]->offset(x % _pageSize);
		}

		size_t capacity() const
		{
			return _pageCount * _pageSize;
		}

		void setCursor(size_t cursor)
		{
			_cursor = cursor;
		}

		size_t cursor() const
		{
			return _cursor;
		}

		size_t pageLimit(ssize_t x)
		{
			return _pageSize - (x % _pageSize);
		}

		void setSize(size_t size)
		{
			_size = size;
		}

		size_t size() const
		{
			return _size;
		}

		~Buffer()
		{
			for (int i = 0; i < _pageCount; i++)
			{
				_arena->put(_pages[i]);
			}
		}
	};

	struct BufferManager
	{
		thread_local static MemoryArena* arena;

		static Buffer* getBuffer()
		{
			Buffer* b = new Buffer(arena);
			b->tryNewPage();
			return b;
		}

		static Buffer* getEmptyBuffer()
		{
			return new Buffer(arena);
		}
	};


}