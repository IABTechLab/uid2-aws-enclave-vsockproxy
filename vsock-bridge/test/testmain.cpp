#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <cstdlib>
#include <functional>
#include <unordered_set>
#include <vector>
#include <iostream>
#include <thread>

#include <socket.h>
#include <threading.h>
#include <channel.h>
#include <dispatcher.h>

#include "mocks.h"

using namespace vsockio;

std::vector<WorkerThread*> ThreadPool::threads;
thread_local MemoryArena* BufferManager::arena = new MemoryArena();

TEST_CASE("Queue works", "[queue]") 
{
	UniquePtrQueue<int> q;

	std::unique_ptr<int> pNumbers[]{
		std::unique_ptr<int>(new int(0)),
		std::unique_ptr<int>(new int(1)),
		std::unique_ptr<int>(new int(2)),
		std::unique_ptr<int>(new int(3)),
		std::unique_ptr<int>(new int(4)),
	};

	for (int i = 0; i < 5; i++)
	{
		q.enqueue(pNumbers[i]);
	}

	for (int i = 0; i < 5; i++)
	{
		auto p = q.dequeue();
		REQUIRE(*p == i);
	}
}

TEST_CASE("Buffer works", "[buffer]")
{
	BufferManager::arena = new MemoryArena();
	BufferManager::arena->init(512, 20);
	auto b = std::unique_ptr<Buffer>(BufferManager::getEmptyBuffer());
	
	REQUIRE(b->size() == 0);
	REQUIRE(b->capacity() == 0);
	REQUIRE(b->cursor() == 0);

	REQUIRE(b->tryNewPage());
	REQUIRE(b->size() == 0);
	REQUIRE(b->capacity() == b->_pageSize);
	for (int i = 0; i < b->_pageSize; i += b->_pageSize / 4)
	{
		REQUIRE(b->pageLimit((ssize_t)i) == b->_pageSize - (i % b->_pageSize));
	}

	ssize_t c = b->_pageSize / 2;
	b->setCursor(c);
	REQUIRE(b->cursor() == c);

	b->setSize(c);
	REQUIRE(b->size() == c);

	REQUIRE(b->tryNewPage());
	REQUIRE(b->capacity() == 2 * b->_pageSize);
	REQUIRE(b->size() == c);
	for (int i = 0; i < b->_pageSize; i += b->_pageSize / 4)
	{
		REQUIRE(b->pageLimit((ssize_t)(b->_pageSize + i) == b->_pageSize - i));
	}
}

// "How to mock socket in C" https://stackoverflow.com/a/44075457/15239363

struct
{
	int lastFd;
	std::unordered_set<int> openFds;

	void reset()
	{
		lastFd = 10;
		openFds.clear();
	}

} testContext;

int mock_sock_default(int domain, int type, int protocol)
{
	int fd = ++testContext.lastFd;
	testContext.openFds.insert(fd);
	return fd;
}

int mock_read_default(int fd, void* buf, int len)
{
	uint8_t* bytes = static_cast<uint8_t*>(buf);
	memset(buf, (int)'a', len * sizeof(uint8_t));
	return len;
}

int mock_write_default(int fd, void* buf, int len)
{
	return len;
}

int mock_close_default(int fd)
{
	return 0;
}

TEST_CASE("Slow write", "[peer]")
{
	std::vector<uint8_t> sink;

	SocketImpl impl;

	int readCount = 0;
	int writeCount = 0;
	int bytesRead = 0;
	int bytesWrite = 0;
	int bytesTotal = 4096;

	impl.read = [&bytesRead, &readCount, bytesTotal](int fd, void* buf, int len) {
		readCount++;
		if (bytesRead < bytesTotal) {
			int t = len < (bytesTotal - bytesRead) ? len : (bytesTotal - bytesRead);
			bytesRead += t;
			return mock_read_default(fd, buf, t);
		}
		return 0;
	};

	impl.write = [&sink, &writeCount, &bytesWrite](int fd, void* buf, int len) {
		writeCount++;
		sink.push_back(*((uint8_t*)buf));
		bytesWrite += 16;
		return 16;
	};

	impl.close = [](int _) {return 0; };
	
	testContext.reset();

	const int AF_INET = 0;
	const int SOCK_STREAM = 0;

	int fd_a = mock_sock_default(AF_INET, SOCK_STREAM, 0);
	int fd_b = mock_sock_default(AF_INET, SOCK_STREAM, 0);
	Socket a(fd_a, &impl);
	Socket b(fd_b, &impl);

	a.setPeer(&b);
	b.setPeer(&a);

	b.onOutputReady();
	a.onInputReady();

	REQUIRE(bytesWrite == bytesTotal);
	REQUIRE(sink[0] == sink[1]);
	REQUIRE(sink[1] == (uint8_t)'a');
}

TEST_CASE("Fast close", "[peer]")
{
	testContext.reset();

	SocketImpl impl;

	impl.read = [](int fd, void* buf, int len) {
		return 0;
	};

	std::vector<uint8_t> sink;

	impl.write = [&sink](int fd, void* buf, int len) {
		sink.push_back(*((uint8_t*)buf));
		return 16;
	};

	impl.close = [](int _) {return 0; };

	const int AF_INET = 0;
	const int SOCK_STREAM = 0;

	int fd_a = mock_sock_default(AF_INET, SOCK_STREAM, 0);
	int fd_b = mock_sock_default(AF_INET, SOCK_STREAM, 0);
	Socket a(fd_a, &impl);
	Socket b(fd_b, &impl);

	a.setPeer(&b);
	b.setPeer(&a);

	b.onOutputReady();
	a.onInputReady();
}

TEST_CASE("Correct content", "[peer]")
{
	testContext.reset();

	SocketImpl impl;

	std::string source = "hello, world, hello, world, hello, world!";
	int bytesRead = 0;
	impl.read = [&bytesRead, &source](int fd, void* buf, int len) {
		if (bytesRead < source.size()) {
			int t = len < (source.size() - bytesRead) ? len : (source.size() - bytesRead);
			memcpy(buf, &(source.c_str()[bytesRead]), t * sizeof(uint8_t));
			bytesRead += t;
			return t;
		}
		return 0;
	};

	std::string dest;
	impl.write = [&dest](int fd, void* buf, int len) {
		for (int i = 0; i < len; i++)
			dest += ((char*)buf)[i];
		return len;
	};

	impl.close = [](int _) { return 0; };

	const int AF_INET = 0;
	const int SOCK_STREAM = 0;

	int fd_a = mock_sock_default(AF_INET, SOCK_STREAM, 0);
	int fd_b = mock_sock_default(AF_INET, SOCK_STREAM, 0);
	Socket a(fd_a, &impl);
	Socket b(fd_b, &impl);

	a.setPeer(&b);
	b.setPeer(&a);

	b.onOutputReady();
	a.onInputReady();

	REQUIRE(dest == source);
}

TEST_CASE("No early close", "[peer]")
{
	testContext.reset();

	std::unordered_set<int> closedFds;
	
	SocketImpl impl;
	std::string source = "hello, world, hello, world, hello, world!";
	int bytesRead = 0;
	impl.read = [&bytesRead, &source, &closedFds](int fd, void* buf, int len) {
		REQUIRE(closedFds.count(fd) == 0);
		if (bytesRead < source.size()) 
		{
			int t = len < (source.size() - bytesRead) ? len : (source.size() - bytesRead);
			memcpy(buf, &(source.c_str()[bytesRead]), t * sizeof(uint8_t));
			bytesRead += t;
			return t;
		}
		return 0;
	};

	std::string dest;
	impl.write = [&dest, &closedFds](int fd, void* buf, int len) {
		REQUIRE(closedFds.count(fd) == 0);
		for (int i = 0; i < len; i++)
			dest += ((char*)buf)[i];
		return len;
	};

	impl.close = [&closedFds](int fd) {
		REQUIRE(closedFds.count(fd) == 0);
		closedFds.insert(fd);
		return 0;
	};

	const int AF_INET = 0;
	const int SOCK_STREAM = 0;

	int fd_a = mock_sock_default(AF_INET, SOCK_STREAM, 0);
	int fd_b = mock_sock_default(AF_INET, SOCK_STREAM, 0);
	Socket a(fd_a, &impl);
	Socket b(fd_b, &impl);

	a.setPeer(&b);
	b.setPeer(&a);

	// read all and close
	a.onInputReady();

	REQUIRE(a.inputClosed());

	b.onOutputReady();

	REQUIRE(dest == source);
}

TEST_CASE("Threaded IO", "[threading]")
{
	WorkerThread* wt = new WorkerThread([](){});
	std::thread t(&WorkerThread::run, wt);

	auto* q = wt->_taskQueue;
	q->enqueue([]() {
		std::cout << "threading test, block worker thread for 1 second" << std::endl;
	});

	std::this_thread::sleep_for(std::chrono::seconds(1));

	q->enqueue([wt]() {
		std::cout << "retiring in 1 second..." << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(1));
		wt->stop();
		std::cout << "worker thread stopped." << std::endl;
	});

	std::cout << "main thread joined." << std::endl;
	t.join();
	std::cout << "main thread exiting." << std::endl;
}

TEST_CASE("Queue tasks in channel", "[channel]")
{
	testContext.reset();

	std::unordered_set<int> closedFds;

	SocketImpl impl;
	std::string source = "hello, world, hello, world, hello, world!";
	int bytesRead = 0;
	impl.read = [&bytesRead, &source, &closedFds](int fd, void* buf, int len) {
		REQUIRE(closedFds.count(fd) == 0);
		if (bytesRead < source.size())
		{
			int t = len < (source.size() - bytesRead) ? len : (source.size() - bytesRead);
			memcpy(buf, &(source.c_str()[bytesRead]), t * sizeof(uint8_t));
			bytesRead += t;
			return t;
		}

		// return error
		errno = ECONNABORTED;
		return -1;
	};

	std::string dest;
	impl.write = [&dest, &closedFds](int fd, void* buf, int len) {
		REQUIRE(closedFds.count(fd) == 0);
		for (int i = 0; i < len; i++)
			dest += ((char*)buf)[i];
		return len;
	};

	impl.close = [&closedFds](int fd) {
		REQUIRE(closedFds.count(fd) == 0);
		closedFds.insert(fd);
		return 0;
	};


	BlockingQueue<std::function<void()>> tQueue;

	const int AF_INET = 0;
	const int SOCK_STREAM = 0;

	int fd_a = mock_sock_default(AF_INET, SOCK_STREAM, 0);
	int fd_b = mock_sock_default(AF_INET, SOCK_STREAM, 0);
	Socket* a = new Socket(fd_a, &impl);
	Socket* b = new Socket(fd_b, &impl);
	DirectChannel c(1, a, b, &tQueue);
	c.handle(b->fd(), IOEvent::InputReady);
	c.handle(a->fd(), IOEvent::OutputReady);

	while (tQueue.count() > 0)
	{
		auto action = tQueue.dequeue();
		action();
	}

	c.handle(b->fd(), IOEvent::OutputReady);
	c.handle(a->fd(), IOEvent::InputReady);
	
	while (tQueue.count() > 0)
	{
		auto action = tQueue.dequeue();
		action();
	}

	REQUIRE(a->ioEventCount() == 0);
	REQUIRE(b->ioEventCount() == 0);
	REQUIRE(a->inputClosed());
	REQUIRE(dest == source);
}


TEST_CASE("Id LinkedList behavior", "[processor]")
{
	ChannelIdList ls;

	// [head][0][1][2]

	auto* id0 = ls.getNode();
	auto* id1 = ls.getNode();
	auto* id2 = ls.getNode();

	REQUIRE(id0->id == 0);
	REQUIRE(id1->id == 1);
	REQUIRE(id2->id == 2);
	REQUIRE(id0->inUse);
	REQUIRE(id1->inUse);
	REQUIRE(id2->inUse);
	
	ls.putNode(id1);
	ls.putNode(id0);
	auto* id1_ = ls.getNode();
	REQUIRE(id1_->id == 1);
	REQUIRE(id1_->inUse);

	auto* id0_ = ls.getNode();
	REQUIRE(id0_->id == 0);
	REQUIRE(id0_->inUse);

	auto* id3_ = ls.getNode();
	REQUIRE(id3_->id == 3);
	REQUIRE(id3_->inUse);
}

void createWorkerThreads(int numThreads)
{
	for (int i = 0; i < numThreads; i++)
	{
		ThreadPool::threads.push_back(new WorkerThread([]() { BufferManager::arena->init(1024, 20); }));
	}
}

void terminateWorkerThreads()
{
	for (int i = 0; i < ThreadPool::threads.size(); i++)
	{
		ThreadPool::threads[i]->stop();
		ThreadPool::threads[i]->t->join();
	}
	ThreadPool::threads.clear();
}

TEST_CASE("Dispatcher", "[dispatcher]")
{
	createWorkerThreads(3);

	std::string source = "hello, world, hello, world, hello, world!";
	std::string dest;
	int bytesRead = 0;
	SocketImpl impl(
		[&bytesRead, &source](int fd, void* buf, int len) {
			if (bytesRead < source.size())
			{
				int t = len < (source.size() - bytesRead) ? len : (source.size() - bytesRead);
				memcpy(buf, &(source.c_str()[bytesRead]), t * sizeof(uint8_t));
				bytesRead += t;
				return t;
			}
			return 0;
		},
		[&dest](int fd, void* buf, int len) {
			for (int i = 0; i < len; i++)
				dest += ((char*)buf)[i];
			return len;
		},
		[](int fd) {
			return 0;
		});

	MockPoller poller(20);
	Dispatcher ex(&poller);
	auto* channel = ex.prepareChannel();
	Socket* client = new Socket(1, &impl);
	Socket* server = new Socket(2, &impl);
	ex.makeChannel(channel, client, server, ThreadPool::getTaskQueue(channel->id));

	REQUIRE(poller._fdMap.find(1) != poller._fdMap.end());
	REQUIRE(poller._fdMap.find(2) != poller._fdMap.end());
	REQUIRE(poller._fdMap[1].listeningEvents == (uint32_t)(IOEvent::InputReady | IOEvent::OutputReady));
	REQUIRE(poller._fdMap[2].listeningEvents == (uint32_t)(IOEvent::InputReady | IOEvent::OutputReady));
	REQUIRE(((ChannelHandle*)poller._fdMap[1].handler)->channelId == channel->id);
	REQUIRE(((ChannelHandle*)poller._fdMap[2].handler)->channelId == channel->id);

	auto* queue = ThreadPool::getTaskQueue(channel->id);
	REQUIRE(queue->empty());
	poller.setInputReady(1, true);
	poller.setOutputReady(2, true);

	ex.scanAndCleanInterval = 1;
	int retry = 0;
	for (; retry < 100 && source != dest; retry++)
	{
		ex.taskloop();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	poller.setOutputReady(2, true);
	ex.taskloop();
	REQUIRE(channel->inUse == false);

	for (int i = 0; i < ThreadPool::threads.size(); i++)
	{
		std::cout << "thread " << i << " processed " << ThreadPool::threads[i]->_eventsProcessed << std::endl;
	}

	terminateWorkerThreads();

	REQUIRE(dest == source);
}