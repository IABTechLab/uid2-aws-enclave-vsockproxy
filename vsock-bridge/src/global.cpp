#include <threading.h>
#include <socket.h>
#include <unistd.h>
#include <sys/socket.h>

using namespace vsockio;

thread_local MemoryArena* BufferManager::arena = new MemoryArena();

SocketImpl* SocketImpl::singleton = new SocketImpl(
	/*read: */  [](int fd, void* buf, int len) { return ::read(fd, buf, len); },
	/*write:*/  [](int fd, void* buf, int len) { return ::write(fd, buf, len); },
	/*close:*/  [](int fd) { return ::close(fd); }
);
