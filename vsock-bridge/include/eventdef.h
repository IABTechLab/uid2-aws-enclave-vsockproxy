#pragma once

#include <cstdint>

namespace vsockio
{
	enum IOEvent : uint8_t
	{
		None = 0,
        InputReady = 0x001,
        OutputReady = 0x004,
        Error = 0x008,
	};

	struct VsbEvent
	{
		IOEvent ioFlags;
		void* data;
	};
}