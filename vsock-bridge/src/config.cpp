#include <config.h>
#include <logger.h>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <iostream>

namespace vsockproxy
{
	/*
	 * this parser only handles a subset of yaml like:
	 *
		---

		socks-proxy:
		  service: socks
		  listen: vsock://-1:3305

		file-server:
		  service: file
		  listen: vsock://-1:3306
		  mapping:
			- config:/etc/uidoperator/config.json
			- secrets:/etc/uidoperator/secrets.json

		operator-service:
		  service: direct
		  listen: tcp://127.0.0.1:8080
		  connect: vsock://35:8080

		operator-prometheus:
		  service: direct
		  listen: tcp://127.0.0.1:9080
		  connect: vsock://35:9080

	 */

	struct yaml_line
	{
		bool is_empty;
		bool is_comment;
		bool is_list_element;
		int level;
		std::string key;
		std::string value;
	};

	std::string name_service_type(service_type t)
	{
		switch (t)
		{
		case service_type::DIRECT_PROXY: return "direct";
		case service_type::SOCKS_PROXY: return "socks";
		case service_type::FILE: return "file";
		default: return "unknown";
		}
	}

	std::string name_scheme(endpoint_scheme t)
	{
		switch (t)
		{
		case endpoint_scheme::TCP4: return "tcp";
		case endpoint_scheme::VSOCK: return "vsock";
		default: return "unknown";
		}
	}


	uint16_t try_str2short(std::string s, uint16_t default_value)
	{
		if (s.size() == 0) return default_value;

		uint16_t value = 0;
		for (int i = 0; i < s.size(); i++)
		{
			if (s[i] >= '0' && s[i] <= '9')
			{
				value *= 10;
				value += s[i] - '0';
			}
			else
			{
				return default_value;
			}
		}

		return value;
	}

	yaml_line nextline(std::ifstream& s)
	{
		yaml_line y;
		y.is_empty = true;
		y.is_comment = false;
		y.is_list_element = false;
		for (std::string line; std::getline(s, line); )
		{
			y.is_empty = true;
			y.is_comment = false;
			y.is_list_element = false;

			if (line == "---") continue;

			int state = 0; // 1 = reading key, 2 = reading value
			std::string key;
			std::string value;
			for (int i = 0; i < line.size(); i++)
			{
				if (line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != '\n')
				{
					if (y.is_empty)
					{
						// first character
						y.is_empty = false;
						y.level = i;
						if (line[i] == '#')
						{
							y.is_comment = true;
							break;
						}
						else if (line[i] == '-')
						{
							y.is_list_element = true;
						}
						state = 1;

						if (y.is_list_element) continue; // skip '-'
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

			if (key.size() > 0)
			{
				y.key = key;
				y.value = value;
				break;
			}
		}
		return y;
	}

	void tryparse_endpoint(std::string& value, endpoint& out_ep)
	{
		size_t p = value.find(':');
		if (p != value.npos)
		{
			std::string scheme = value.substr(0, p);
			if (scheme == "vsock")
			{
				out_ep.scheme = endpoint_scheme::VSOCK;
			}
			else if (scheme == "tcp")
			{
				out_ep.scheme = endpoint_scheme::TCP4;
			}
		}
		p += 3; // skip '://'

		size_t p2 = value.find(':', p);
		if (p2 != value.npos)
		{
			out_ep.address = value.substr(p, p2 - p);
			out_ep.port = try_str2short(value.substr(p2 + 1), 0);
		}
	}

	std::vector<service_description> load_config(std::string filepath)
	{
		std::vector<service_description> services;

		std::ifstream f;
		f.open(filepath);

		if (!f.good())
		{
			Logger::instance->Log(Logger::CRITICAL, "configuration file not accessible");
			return services;
		}

		int level_indent = -1;

		service_description cs;
		cs.type = service_type::UNKNOWN;
		cs.listen_ep.scheme = endpoint_scheme::UNKNOWN;
		while (true)
		{
			yaml_line line = nextline(f);
			if (line.is_empty) break;

			if (line.level == 0)
			{
				if (cs.type != service_type::UNKNOWN)
				{
					services.push_back(cs);
				}
				cs = service_description();
				cs.type = service_type::UNKNOWN;
				cs.listen_ep.scheme = endpoint_scheme::UNKNOWN;
				cs.name = line.key;
			}
			else
			{
				if (level_indent == -1)
				{
					// first time we find non-zero indentation,
					// use this to determine level
					level_indent = line.level;
				}

				int level = line.level / level_indent;
				if (level == 1)
				{
					if (line.key == "service")
					{
						if (line.value == "socks")
							cs.type = service_type::SOCKS_PROXY;
						else if (line.value == "file")
							cs.type = service_type::FILE;
						else if (line.value == "direct")
							cs.type = service_type::DIRECT_PROXY;
						else
							cs.type = service_type::UNKNOWN;
					}
					else if (line.key == "listen")
					{
						tryparse_endpoint(line.value, cs.listen_ep);
					}
					else if (line.key == "connect")
					{
						tryparse_endpoint(line.value, cs.connect_ep);
					}
				}
				else if (level == 2)
				{
					if (line.is_list_element)
					{
						cs.mapping.push_back(std::make_pair(line.key, line.value));
					}
				}
			}
		}

		if (cs.type != service_type::UNKNOWN)
		{
			services.push_back(cs);
		}

		return services;
	}

	std::string describe(service_description& sd)
	{
		std::stringstream ss;
		ss << sd.name
			<< "\n  type: " << name_service_type(sd.type)
			<< "\n  listen: " << name_scheme(sd.listen_ep.scheme) << "://" << sd.listen_ep.address << ":" << sd.listen_ep.port
			<< "\n  connect: " << name_scheme(sd.connect_ep.scheme) << "://" << sd.connect_ep.address << ":" << sd.connect_ep.port
			<< "\n  mapping:";

		for (auto& p : sd.mapping)
		{
			ss << "\n    - " << p.first << ":" << p.second;
		}

		return ss.str();
	}

}