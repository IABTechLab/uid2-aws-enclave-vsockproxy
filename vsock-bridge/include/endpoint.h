#pragma once

#include <string>

#include <arpa/inet.h>
#include <linux/vm_sockets.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

namespace vsockio
{
	struct Endpoint
	{
		virtual int getSocket() = 0;
		virtual std::pair<const sockaddr*, socklen_t> getAddress() = 0;
		virtual std::pair<sockaddr*, socklen_t> getWritableAddress() = 0;
		virtual std::string describe() = 0;
		virtual std::unique_ptr<Endpoint> clone() = 0;
		virtual ~Endpoint() {}
	};

	struct TCP4Endpoint : public Endpoint
	{
		TCP4Endpoint(std::string ip, int port) : _ipAddress(ip), _port(port) 
		{
			memset(&_saddr, 0, sizeof(_saddr));
			_saddr.sin_family = AF_INET;
			_saddr.sin_port = htons((uint16_t)_port);
			_saddr.sin_addr.s_addr = address();
		}

		int getSocket() override
		{
			return socket(AF_INET, SOCK_STREAM, 0);
		}

		std::pair<const sockaddr*, socklen_t> getAddress() override
		{
			return std::make_pair((sockaddr*)&_saddr, sizeof(_saddr));
		}

		std::pair<sockaddr*, socklen_t> getWritableAddress() override
		{
			_saddr.sin_family = AF_INET;
			return std::make_pair((sockaddr*)&_saddr, sizeof(_saddr));
		}

		std::string describe() override
		{
			char buf[20];
			inet_ntop(AF_INET, &_saddr.sin_addr.s_addr, buf, sizeof(buf));
			return "tcp4://" + std::string(buf) + ":" + std::to_string(ntohs(_saddr.sin_port));
		}

		std::unique_ptr<Endpoint> clone() override
		{
			return std::unique_ptr<Endpoint>(new TCP4Endpoint(_ipAddress, _port));
		}

		in_addr_t address() const
		{
			in_addr_t sa;
			inet_pton(AF_INET, _ipAddress.c_str(), &(sa));
			return sa;
		}

		sockaddr_in _saddr;
		std::string _ipAddress;
		uint16_t _port;
	};

	struct VSockEndpoint : public Endpoint
	{
		VSockEndpoint(int cid, int port) : _cid(cid), _port(port)
		{
			memset(&_saddr, 0, sizeof(_saddr));
			_saddr.svm_family = AF_VSOCK;
			_saddr.svm_cid = _cid;				// in host byte order
			_saddr.svm_port = _port;            // in host byte order
		}

		int getSocket() override
		{
			return socket(AF_VSOCK, SOCK_STREAM, 0);
		}

		std::pair<const sockaddr*, socklen_t> getAddress() override
		{
			return std::make_pair((sockaddr*)&_saddr, sizeof(_saddr));
		}

		std::pair<sockaddr*, socklen_t> getWritableAddress() override
		{
			_saddr.svm_family = AF_VSOCK;
			return std::make_pair((sockaddr*)&_saddr, sizeof(_saddr));
		}

		std::string describe() override
		{
			return "vsock://" + std::to_string(_cid) + ":" + std::to_string(_port);
		}

		std::unique_ptr<Endpoint> clone() override
		{
			return std::unique_ptr<Endpoint>(new VSockEndpoint(_cid, 0));
		}

		sockaddr_vm _saddr;
		int _cid;
		uint16_t _port;
	};

}