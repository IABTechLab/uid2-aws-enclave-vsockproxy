#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vsockproxy
{
	enum class service_type : uint8_t
	{
		UNKNOWN = 0,
		SOCKS_PROXY,
		FILE,
		DIRECT_PROXY,
	};

	enum class endpoint_scheme : uint8_t
	{
		UNKNOWN = 0,
		VSOCK,
		TCP4,
	};

	struct endpoint
	{
		endpoint_scheme scheme;
		std::string address;
		uint16_t port;
	};

	struct service_description
	{
		std::string name;
		service_type type;
		endpoint listen_ep;
		endpoint connect_ep;
		std::vector<std::pair<std::string, std::string>> mapping;
	};

	uint16_t try_str2short(std::string s, uint16_t default_value);

	std::vector<service_description> load_config(std::string filepath);

	std::string describe(service_description& sd);
}