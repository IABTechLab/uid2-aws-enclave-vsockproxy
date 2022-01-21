#pragma once

#include <string>
#include <iostream>
#include <ostream>
#include <fstream>
#include <sstream>
#include <mutex>
#include <syslog.h>

struct LoggingStream 
{
	virtual std::ostream& getStream(int level) = 0;
};

struct Logger
{
	enum {
		DEBUG = 0,
		INFO = 1,
		WARNING = 2,
		ERROR = 3,
		CRITICAL = 4,
	};

	int _minLevel;
	static Logger* instance;
	std::mutex _lock;
	LoggingStream* _streamProvider;

	Logger() : _streamProvider(nullptr), _minLevel(DEBUG) {}

	void setMinLevel(int minLevel)
	{
		_minLevel = minLevel;
	}

	void setStreamProvider(LoggingStream* streamProvider)
	{
		_streamProvider = streamProvider;
	}

	template <typename T0>
	void Log(int level, T0&& m0)
	{
		if (level < _minLevel || _streamProvider == nullptr) return;
		std::lock_guard<std::mutex> lk(_lock);
		_streamProvider->getStream(level) << m0 << std::endl;
	}

	template <typename T0, typename T1>
	void Log(int level, T0&& m0, T1&& m1)
	{
		if (level < _minLevel || _streamProvider == nullptr) return;
		std::lock_guard<std::mutex> lk(_lock);
		_streamProvider->getStream(level) << m0 << m1 << std::endl;
	}

	template <typename T0, typename T1, typename T2>
	void Log(int level, T0&& m0, T1&& m1, T2&& m2)
	{
		if (level < _minLevel || _streamProvider == nullptr) return;
		std::lock_guard<std::mutex> lk(_lock);
		_streamProvider->getStream(level) << m0 << m1 << m2 << std::endl;
	}

	template <typename T0, typename T1, typename T2, typename T3>
	void Log(int level, T0&& m0, T1&& m1, T2&& m2, T3&& m3)
	{
		if (level < _minLevel || _streamProvider == nullptr) return;
		std::lock_guard<std::mutex> lk(_lock);
		_streamProvider->getStream(level) << m0 << m1 << m2 << m3 << std::endl;
	}

	template <typename T0, typename T1, typename T2, typename T3, typename T4>
	void Log(int level, T0&& m0, T1&& m1, T2&& m2, T3&& m3, T4&& m4)
	{
		if (level < _minLevel || _streamProvider == nullptr) return;
		std::lock_guard<std::mutex> lk(_lock);
		_streamProvider->getStream(level) << m0 << m1 << m2 << m3 << m4 << std::endl;
	}

	template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5>
	void Log(int level, T0&& m0, T1&& m1, T2&& m2, T3&& m3, T4&& m4, T5&& m5)
	{
		if (level < _minLevel || _streamProvider == nullptr) return;
		std::lock_guard<std::mutex> lk(_lock);
		_streamProvider->getStream(level) << m0 << m1 << m2 << m3 << m4 << m5 << std::endl;
	}
};


class RSyslogBuf : public std::stringbuf
{
public:
	virtual int sync() override
	{
		std::lock_guard<std::mutex> lock(mut);
		syslog(log_level, this->str().c_str());
		this->str(std::string());
		return 0;
	}
	RSyslogBuf(int level) : log_level(level) {}

private:
	std::mutex mut;
	int log_level;
};

struct NullStream : public std::ostream {
	struct NullBuffer : public std::streambuf {
		int overflow(int c) { return c; }
	} m_nb;
	NullStream() : std::ostream(&m_nb) {}
};

struct StdoutLogger : public LoggingStream
{
	std::ostream& getStream(int level) override
	{
		return std::cout;
	}
};

struct RSyslogLogger : public LoggingStream
{
	std::ostream& getStream(int level) override
	{
		if (level == Logger::DEBUG) return debug;
		if (level == Logger::INFO) return info;
		if (level == Logger::WARNING) return warn;
		if (level == Logger::ERROR) return error;
		if (level == Logger::CRITICAL) return critical;
		return null_stream;
	}

	RSyslogLogger(const char* name)
		: debugb(LOG_DEBUG), infob(LOG_INFO), warnb(LOG_WARNING), errorb(LOG_ERR), criticalb(LOG_CRIT)
		, debug(&debugb), info(&infob), warn(&warnb), error(&errorb), critical(&criticalb)
	{
		openlog(name, LOG_CONS | LOG_PID | LOG_NDELAY, LOG_USER);
	}

	~RSyslogLogger()
	{
		closelog();
	}

	std::ostream debug;
	std::ostream info;
	std::ostream warn;
	std::ostream error;
	std::ostream critical;
	RSyslogBuf debugb;
	RSyslogBuf infob;
	RSyslogBuf warnb;
	RSyslogBuf errorb;
	RSyslogBuf criticalb;
	NullStream null_stream;
};