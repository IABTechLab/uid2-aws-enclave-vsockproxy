#include "config.h"
#include "logger.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>

namespace vsockproxy
{
	/*
	 * this parser only handles a subset of yaml like:
	 *
		---

		socks-proxy:
		  service: socks
		  listen: vsock://-1:3305
		  connect: tcp://127.0.0.1:3306

		operator-service:
		  service: direct
		  listen: tcp://127.0.0.1:8080
		  connect: vsock://35:8080

		operator-prometheus:
		  service: direct
		  listen: tcp://127.0.0.1:9080
		  connect: vsock://35:9080

	 */

	struct YamlLine
	{
		bool _isEmpty;
		bool _isListElement;
		int _level;
		std::string _key;
		std::string _value;
	};

	static std::string nameServiceType(ServiceType t)
	{
		switch (t)
		{
		case ServiceType::DIRECT_PROXY: return "direct";
		default: return "unknown";
		}
	}

    static std::string nameEndpointScheme(EndpointScheme t)
	{
		switch (t)
		{
		case EndpointScheme::TCP4: return "tcp";
		case EndpointScheme::VSOCK: return "vsock";
		default: return "unknown";
		}
	}


    static std::optional<uint16_t> trystrtous(const std::string& s)
	{
		if (s.empty()) return std::nullopt;

        try
        {
            const auto result = std::stoul(s);
            if (result > std::numeric_limits<uint16_t>::max())
            {
                return std::nullopt;
            }
            return static_cast<uint16_t>(result);
        }
        catch (...)
        {
            return std::nullopt;
        }
	}

    static YamlLine nextLine(std::ifstream& s)
	{
        YamlLine y;
		y._isEmpty = true;
		y._isListElement = false;
		for (std::string line; std::getline(s, line); )
		{
			y._isEmpty = true;
			y._isListElement = false;

			if (line == "---") continue;

			int state = 0; // 1 = reading key, 2 = reading value
			std::string key;
			std::string value;
			for (int i = 0; i < line.size(); i++)
			{
				if (line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != '\n')
				{
					if (y._isEmpty)
					{
						// first character
						y._isEmpty = false;
						y._level = i;
						if (line[i] == '#')
						{
							break;
						}
						else if (line[i] == '-')
						{
							y._isListElement = true;
						}
						state = 1;

						if (y._isListElement) continue; // skip '-'
					}

					if (state == 1)
					{
						if (line[i] == ':')
						{
							state = 2;
							continue;
						}
						else
						{
							key.push_back(line[i]);
						}
					}
					if (state == 2)
					{
						value.push_back(line[i]);
					}
				}
			}

			if (!key.empty())
			{
				y._key = key;
				y._value = value;
				break;
			}
		}
		return y;
	}

    static std::optional<EndpointConfig> tryParseEndpoint(const std::string& value)
	{
        EndpointConfig endpointConfig;
		size_t p = value.find(':');
		if (p != value.npos)
		{
			const std::string scheme = value.substr(0, p);
			if (scheme == "vsock")
			{
                endpointConfig._scheme = EndpointScheme::VSOCK;
			}
			else if (scheme == "tcp")
			{
                endpointConfig._scheme = EndpointScheme::TCP4;
			}
		}
		p += 3; // skip '://'

		const size_t p2 = value.find(':', p);
		if (p2 != value.npos)
		{
            endpointConfig._address = value.substr(p, p2 - p);
            const auto port = trystrtous(value.substr(p2 + 1));
            if (!port)
            {
                Logger::instance->Log(Logger::CRITICAL, "invalid port number: ", value.substr(p2 + 1));
                return std::nullopt;
            }
            endpointConfig._port = *port;
		}

        return endpointConfig;
	}

	std::vector<ServiceDescription> loadConfig(const std::string& filepath)
	{
		std::vector<ServiceDescription> services;

		std::ifstream f;
		f.open(filepath);

		if (!f.good())
		{
			Logger::instance->Log(Logger::CRITICAL, "configuration file not accessible");
			return services;
		}

		int levelIndent = -1;

		ServiceDescription cs;
		while (true)
		{
			YamlLine line = nextLine(f);
			if (line._isEmpty) break;

			if (line._level == 0)
			{
				if (cs._type != ServiceType::UNKNOWN)
				{
					services.push_back(cs);
				}
				cs = ServiceDescription();
				cs._name = line._key;
			}
			else
			{
				if (levelIndent == -1)
				{
					// first time we find non-zero indentation,
					// use this to determine level
					levelIndent = line._level;
				}

				const int level = line._level / levelIndent;
				if (level == 1)
				{
					if (line._key == "service")
					{
						if (line._value == "direct")
							cs._type = ServiceType::DIRECT_PROXY;
						else
                        {
                            Logger::instance->Log(Logger::CRITICAL, "unknown service type for service: ", cs._name);
                            return {};
                        }
					}
					else if (line._key == "listen")
					{
						const auto endpoint = tryParseEndpoint(line._value);
                        if (!endpoint)
                        {
                            Logger::instance->Log(Logger::CRITICAL, "failed to parse listen endpoint config: ", line._value, " for service: ", cs._name);
                            return {};
                        }
                        cs._listenEndpoint = *endpoint;
					}
					else if (line._key == "connect")
					{
                        const auto endpoint = tryParseEndpoint(line._value);
                        if (!endpoint)
                        {
                            Logger::instance->Log(Logger::CRITICAL, "failed to parse connect endpoint config: ", line._value, " for service: ", cs._name);
                            return {};
                        }
                        cs._connectEndpoint = *endpoint;
					}
					else if (line._key == "acceptRcvBuf")
					{
                        cs._acceptRcvBuf = std::stoi(line._value);
					}
					else if (line._key == "acceptSndBuf")
					{
                        cs._acceptSndBuf = std::stoi(line._value);
					}
					else if (line._key == "peerRcvBuf")
					{
                        cs._peerRcvBuf = std::stoi(line._value);
					}
					else if (line._key == "peerSndBuf")
					{
                        cs._peerSndBuf = std::stoi(line._value);
					}
				}
			}
		}

		if (cs._type != ServiceType::UNKNOWN)
		{
			services.push_back(cs);
		}

		return services;
	}

	std::string describe(const ServiceDescription& sd)
	{
		std::stringstream ss;
		ss << sd._name
			<< "\n  type: " << nameServiceType(sd._type)
			<< "\n  listen: " << nameEndpointScheme(sd._listenEndpoint._scheme) << "://" << sd._listenEndpoint._address << ":" << sd._listenEndpoint._port
			<< "\n  connect: " << nameEndpointScheme(sd._connectEndpoint._scheme) << "://" << sd._connectEndpoint._address << ":" << sd._connectEndpoint._port;

		return ss.str();
	}

}