#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vsockproxy
{
	enum class ServiceType : uint8_t
	{
		UNKNOWN = 0,
		DIRECT_PROXY,
	};

	enum class EndpointScheme : uint8_t
	{
		UNKNOWN = 0,
		VSOCK,
		TCP4,
	};

	struct EndpointConfig
	{
		EndpointScheme _scheme = EndpointScheme::UNKNOWN;
		std::string _address;
		uint16_t _port = 0;
	};

	struct ServiceDescription
	{
		std::string _name;
		ServiceType _type = ServiceType::UNKNOWN;
		EndpointConfig _listenEndpoint;
		EndpointConfig _connectEndpoint;
		int _acceptRcvBuf;
		int _acceptSndBuf;
		int _peerRcvBuf;
		int _peerSndBuf;
	};

	std::vector<ServiceDescription> loadConfig(const std::string& filepath);

	std::string describe(const ServiceDescription& sd);
}