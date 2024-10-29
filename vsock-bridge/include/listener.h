#pragma once

#include "channel.h"
#include "dispatcher.h"
#include "endpoint.h"
#include "epoll_poller.h"
#include "logger.h"

#include <cstdint>

#include <arpa/inet.h>
#include <errno.h>
#include <linux/vm_sockets.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace vsockio
{
	struct IOControl {
		static bool setNonBlocking(int fd) {
			const int flags = fcntl(fd, F_GETFL, 0);
			if (flags == -1) {
				const int err = errno;
				Logger::instance->Log(Logger::ERROR, "fcntl error: ", strerror(err));
				return false;
			}
			if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
				const int err = errno;
				Logger::instance->Log(Logger::ERROR, "fcntl error: ", strerror(err));
				return false;
			}
			return true;
		}

		static int setBlocking(int fd) {
			const int flags = fcntl(fd, F_GETFL, 0);
			if (flags == -1) {
				int err = errno;
				Logger::instance->Log(Logger::ERROR, "fcntl error: ", strerror(err));
				return false;
			}
			if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
				int err = errno;
				Logger::instance->Log(Logger::ERROR, "fcntl error: ", strerror(err));
				return false;
			}
			return true;
		}

        static bool setTcpNoDelay(int fd) {
            int enable = 1;
            if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)) < 0)
            {
                const int err = errno;
                Logger::instance->Log(Logger::ERROR, "setsockopt error: ", strerror(err));
                return false;
            }
            return true;
        }
	};

    struct Listener
    {
        const int MAX_POLLER_EVENTS = 256;
        const int SO_BACKLOG = 64;

        Listener(std::unique_ptr<Endpoint>&& listenEndpoint, std::unique_ptr<Endpoint>&& connectEndpoint, std::vector<Dispatcher*>& dispatchers)
            : _fd(-1)
            , _listenEp(std::move(listenEndpoint))
            , _connectEp(std::move(connectEndpoint))
            , _events(new VsbEvent[MAX_POLLER_EVENTS])
            , _listenEpClone(_listenEp->clone())
            , _dispatchers(dispatchers)
            , _dispatcherIdRr(0)
        {
			const int fd = _listenEp->getSocket();
			if (fd < 0)
			{
				throw std::runtime_error("failed to get listener socket");
			}
            
            int enable = 1;
            if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
            {
				close(fd);
				throw std::runtime_error("error setting SO_REUSEADDR");
            }

            std::pair<const sockaddr*, socklen_t> addressAndLen = _listenEp->getAddress();

            if (bind(fd, addressAndLen.first, addressAndLen.second) < 0)
            {
				const int err = errno;
				close(fd);
				Logger::instance->Log(Logger::ERROR, "failed to bind on ", _listenEp->describe(), ": ", strerror(err));
				throw std::runtime_error("failed to bind");
            }

            /* listener fd is blocking intentially */
			if (!IOControl::setBlocking(fd))
			{
				throw std::runtime_error("failed to set blocking");
			}

            _fd = fd;
        }

		Listener(const Listener&) = delete;
		Listener& operator=(const Listener&) = delete;

		~Listener()
		{
			if (_fd >= 0)
			{
				close(_fd);
			}
		}

        void run()
        {
            if (listen(_fd, SO_BACKLOG) < 0)
            {
                const int err = errno;
                Logger::instance->Log(Logger::ERROR, "failed to listen on ", _listenEp->describe(), ": ", strerror(err));
				throw std::runtime_error("failed to listen");
            }

            Logger::instance->Log(Logger::INFO, "listening on ", _listenEp->describe(), ", fd=", _fd);

            // accept loop
            for (;;)
            {
                acceptConnection();
            }
        }

        void acceptConnection()
        {
            // accepted connection should have the same protocol with listen endpoint
            auto addrAndLen = _listenEpClone->getWritableAddress();
            const int clientFd = accept(_fd, addrAndLen.first, &addrAndLen.second);

            if (clientFd == -1)
            {
                const int err = errno;
                if (err == EAGAIN || err == EWOULDBLOCK)
                {
                    // nothing to accept
                    return;
                }
                else
                {
                    // accept failed
                    Logger::instance->Log(Logger::ERROR, "error during accept (fd=", _fd, "): ", strerror(err));
                    return;
                }
            }

			auto inPeer = std::make_unique<Socket>(clientFd, *SocketImpl::singleton);
			if (!IOControl::setNonBlocking(clientFd))
			{
				Logger::instance->Log(Logger::ERROR, "failed to set non-blocking mode (fd=", clientFd, ")");
				return;
			}

            if (_listenEp->getAddress().first->sa_family == AF_INET && !IOControl::setTcpNoDelay(clientFd))
            {
                Logger::instance->Log(Logger::ERROR, "failed to turn off Nagle algorithm (fd=", clientFd, ")");
                return;
            }

            auto outPeer = connectToPeer();
			if (!outPeer)
			{
				return;
			}


            const int dpId = (_dispatcherIdRr++) % _dispatchers.size();
            auto* const dp = _dispatchers[dpId];

			Logger::instance->Log(Logger::DEBUG, "Dispatcher ", dpId, " will handle channel for accepted connection fd=", inPeer->fd(), ", peer fd=", outPeer->fd());
			dp->postAddChannel(std::move(inPeer), std::move(outPeer));
		}

        std::unique_ptr<Socket> connectToPeer()
		{
            const int fd = _connectEp->getSocket();
            if (fd == -1)
            {
                Logger::instance->Log(Logger::ERROR, "creating remote socket failed");
                return nullptr;
            }

			auto peer = std::make_unique<Socket>(fd, *SocketImpl::singleton);

            if (!IOControl::setNonBlocking(fd))
			{
				Logger::instance->Log(Logger::ERROR, "failed to set non-blocking mode (fd=", fd, ")");
				return nullptr;
			}

            if (_connectEp->getAddress().first->sa_family == AF_INET && !IOControl::setTcpNoDelay(fd))
            {
                Logger::instance->Log(Logger::ERROR, "failed to turn off Nagle algorithm (fd=", fd, ")");
                return nullptr;
            }

            auto addrAndLen = _connectEp->getAddress();
            int status = connect(fd, addrAndLen.first, addrAndLen.second);
            if (status == 0 || (status = errno) == EINPROGRESS)
            {
                Logger::instance->Log(Logger::DEBUG, "connected to remote endpoint (fd=", fd, ") with status=", status);
				return peer;
            }
            else
            {
                Logger::instance->Log(Logger::WARNING, "failed to connect to remote endpoint (fd=", fd, "): ", strerror(status));
				return nullptr;
            }
        }

        inline bool listening() const { return _fd >= 0; }

        int _fd;
        std::unique_ptr<Endpoint> _listenEp;
        std::unique_ptr<Endpoint> _listenEpClone;
        std::unique_ptr<Endpoint> _connectEp;
        std::unique_ptr<VsbEvent[]> _events;
        std::vector<Dispatcher*>& _dispatchers;
        uint32_t _dispatcherIdRr;
    };
}