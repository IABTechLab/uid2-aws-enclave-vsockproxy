#pragma once

#include <stdint.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/vm_sockets.h>
#include <logger.h>
#include <sys/fcntl.h>
#include <endpoint.h>
#include <epoll_poller.h>
#include <channel.h>
#include <dispatcher.h>

namespace vsockio
{
    struct IOControl
    {
        static int setNonBlocking(int fd)
        {
            int flags = fcntl(fd, F_GETFL, 0);
            if (flags == -1) 
            {
                int err = errno;
                Logger::instance->Log(Logger::ERROR, "fcntl error: ", strerror(err));
                return -1;
            }
            if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) 
            {
                int err = errno;
                Logger::instance->Log(Logger::ERROR, "fcntl error: ", strerror(err));
                return -1;
            }
            return 0;
        }

        static int setBlocking(int fd)
        {
            int flags = fcntl(fd, F_GETFL, 0);
            if (flags == -1)
            {
                int err = errno;
                Logger::instance->Log(Logger::ERROR, "fcntl error: ", strerror(err));
                return -1;
            }
            if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1)
            {
                int err = errno;
                Logger::instance->Log(Logger::ERROR, "fcntl error: ", strerror(err));
                return -1;
            }
            return 0;
        }
    };

    struct Listener
    {
        const int MAX_POLLER_EVENTS = 256;
        const int SO_BACKLOG = 5;

        Listener(std::unique_ptr<Endpoint>&& listenEndpoint, std::unique_ptr<Endpoint>&& connectEndpoint, std::vector<Dispatcher*>& dispatchers)
            : _fd(-1)
            , _listenEp(std::move(listenEndpoint))
            , _connectEp(std::move(connectEndpoint))
            , _events(new VsbEvent[MAX_POLLER_EVENTS])
            , _listenEpClone(_listenEp->clone())
            , _dispatchers(dispatchers)
            , _dispatcherIdRr(0)
        {
            int fd = _listenEp->getSocket();
            
            int enable = 1;
            if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
            {
                Logger::instance->Log(Logger::ERROR, "error setting SO_REUSEADDR");
                close(fd);
            }

            std::pair<const sockaddr*, socklen_t> addressAndLen = _listenEp->getAddress();

            if (bind(fd, addressAndLen.first, addressAndLen.second) < 0)
            {
                int err = errno;
                Logger::instance->Log(Logger::ERROR, "Failed to bind new Listener on ", _listenEp->describe(), ": ", strerror(err));
                close(fd);
                return;
            }

            /* listener fd is blocking intentially */
            IOControl::setBlocking(fd);

            _fd = fd;
        }

        void run()
        {
            if (listen(_fd, SO_BACKLOG) < 0)
            {
                int err = errno;
                Logger::instance->Log(Logger::ERROR, "Failed to listen on ", _listenEp->describe(), ": ", strerror(err));
                close(_fd);
                return;
            }

            Logger::instance->Log(Logger::DEBUG, "listening on ", _listenEp->describe(), ", fd=", _fd);

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
            int clientFd = accept(_fd, addrAndLen.first, &addrAndLen.second);

            if (clientFd == -1)
            {
                int err = errno;
                if (err == EAGAIN || err == EWOULDBLOCK)
                {
                    // nothing to accept
                    return;
                }
                else
                {
                    // accept failed
                    Logger::instance->Log(Logger::ERROR, "error during accept: ", strerror(err));
                    return;
                }
            }
            IOControl::setNonBlocking(clientFd);

            Socket* socket = new Socket(clientFd, SocketImpl::singleton);

            int dpId = (_dispatcherIdRr++) % _dispatchers.size();
            auto* dp = _dispatchers[dpId];

            ChannelIdListNode* idHandle = dp->prepareChannel();
            Logger::instance->Log(Logger::DEBUG, "Dispatcher ", dpId, " will handle channel ", idHandle->id);

            // Dispatcher::taskloop manages the channel map attached to the dispatcher
            // connectToPeer modifies the map so we request taskloop thread to run it
            dp->runOnTaskLoop([this, socket, idHandle, dp]() { connectToPeer(socket, idHandle, dp); });
        }

        void connectToPeer(Socket* inPeer, ChannelIdListNode* idHandle, Dispatcher* dispatcher)
        {
            int fd = _connectEp->getSocket();
            if (fd == -1)
            {
                Logger::instance->Log(Logger::ERROR, "creating new socket failed.");
                inPeer->shutdown();
                delete inPeer;
                return;
            }

            IOControl::setNonBlocking(fd);

            auto addrAndLen = _connectEp->getAddress();
            int status = connect(fd, addrAndLen.first, addrAndLen.second);
            if (status == 0 || (status = errno) == EINPROGRESS)
            {
                Logger::instance->Log(Logger::DEBUG, "connected to remote endpoint with status=", status);
                Socket* outPeer = new Socket(fd, SocketImpl::singleton);
                dispatcher->makeChannel(idHandle, inPeer, outPeer, ThreadPool::getTaskQueue(idHandle->id));
            }
            else
            {
                Logger::instance->Log(Logger::WARNING, "failed to connect to remote endpoint");
                close(fd);
                inPeer->shutdown();
                delete inPeer;
            }
        }

        inline bool listening() const { return _fd > 0; }

        int _fd;
        std::unique_ptr<Endpoint> _listenEp;
        std::unique_ptr<Endpoint> _listenEpClone;
        std::unique_ptr<Endpoint> _connectEp;
        std::unique_ptr<VsbEvent[]> _events;
        std::vector<Dispatcher*>& _dispatchers;
        uint32_t _dispatcherIdRr;
    };
}