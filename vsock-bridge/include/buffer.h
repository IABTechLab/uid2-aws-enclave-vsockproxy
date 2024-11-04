#pragma once

#include <array>
#include <cassert>
#include <cstdint>

namespace vsockio
{
	struct Buffer
	{
        static constexpr int BUFFER_SIZE = 10240;
        std::array<std::uint8_t, BUFFER_SIZE> _data;
        std::uint8_t* _head = _data.data();
        std::uint8_t* _tail = _data.data();

        std::uint8_t* head() const
        {
            return _head;
        }

		std::uint8_t* tail() const
		{
			return _tail;
		}

        bool hasRemainingCapacity() const
        {
            return _tail < _data.end();
        }

		int remainingCapacity() const
		{
			return _data.end() - _tail;
		}

        int remainingDataSize() const
        {
            return _tail - _head;
        }

		void produce(int size)
		{
            assert(remainingCapacity() >= size);
            _tail += size;
		}

		void consume(int size)
		{
            assert(remainingDataSize() >= size);
			_head += size;
		}

        void reset()
        {
            _head = _tail = _data.data();
        }

		bool consumed() const
		{
			return _head >= _tail;
		}
	};
}
