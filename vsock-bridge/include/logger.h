#pragma once

#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>

#include <syslog.h>

struct LoggingStream 
{
	virtual std::ostream& startLog(int level) = 0;
};

struct Logger {
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

	void setMinLevel(int minLevel) {
		_minLevel = minLevel;
	}

	static const char *getLogLevelStr(int level)
	{
		switch (level)
		{
			case DEBUG: return "DEBG";
			case INFO: return "INFO";
			case WARNING: return "WARN";
			case ERROR: return "ERRR";
			case CRITICAL: return "CRIT";
			default: return "UNKN";
		}
	}

	void setStreamProvider(LoggingStream* streamProvider)
	{
		_streamProvider = streamProvider;
	}

	template <typename T0>
	void Log(int level, const T0& m0)
	{
		if (level < _minLevel || _streamProvider == nullptr) return;
		std::lock_guard<std::mutex> lk(_lock);
		_streamProvider->startLog(level) << m0 << std::endl;
	}

	template <typename T0, typename T1>
	void Log(int level, const T0& m0, const T1& m1)
	{
		if (level < _minLevel || _streamProvider == nullptr) return;
		std::lock_guard<std::mutex> lk(_lock);
		_streamProvider->startLog(level) << m0 << m1 << std::endl;
	}

	template <typename T0, typename T1, typename T2>
	void Log(int level, const T0& m0, const T1& m1, const T2& m2)
	{
		if (level < _minLevel || _streamProvider == nullptr) return;
		std::lock_guard<std::mutex> lk(_lock);
		_streamProvider->startLog(level) << m0 << m1 << m2 << std::endl;
	}

	template <typename T0, typename T1, typename T2, typename T3>
	void Log(int level, const T0& m0, const T1& m1, const T2& m2, const T3& m3)
	{
		if (level < _minLevel || _streamProvider == nullptr) return;
		std::lock_guard<std::mutex> lk(_lock);
		_streamProvider->startLog(level) << m0 << m1 << m2 << m3 << std::endl;
	}

	template <typename T0, typename T1, typename T2, typename T3, typename T4>
	void Log(int level, const T0& m0, const T1& m1, const T2& m2, const T3& m3, const T4& m4)
	{
		if (level < _minLevel || _streamProvider == nullptr) return;
		std::lock_guard<std::mutex> lk(_lock);
		_streamProvider->startLog(level) << m0 << m1 << m2 << m3 << m4 << std::endl;
	}

	template <typename T0, typename T1, typename T2, typename T3, typename T4, typename T5>
	void Log(int level, const T0& m0, const T1& m1, const T2& m2, const T3& m3, const T4& m4, const T5& m5)
	{
		if (level < _minLevel || _streamProvider == nullptr) return;
		std::lock_guard<std::mutex> lk(_lock);
		_streamProvider->startLog(level) << m0 << m1 << m2 << m3 << m4 << m5 << std::endl;
	}
};


class RSyslogBuf : public std::stringbuf
{
public:
	virtual int sync() override
	{
		std::lock_guard<std::mutex> lock(_mut);
		syslog(_logLevel, this->str().c_str());
		this->str(std::string());
		return 0;
	}
	explicit RSyslogBuf(int level) : _logLevel(level) {}

private:
	std::mutex _mut;
	int _logLevel;
};

struct NullStream : public std::ostream {
	struct NullBuffer : public std::streambuf {
		int overflow(int c) { return c; }
	} m_nb;
	NullStream() : std::ostream(&m_nb) {}
};

struct StdoutLogger : public LoggingStream
{
	std::ostream& startLog(int level) override
	{
		const std::time_t t = std::time(0);
		const std::tm* const now = std::localtime(&t);
		auto& s = std::cout;
		const auto prevfill = s.fill('0');
		s << std::setw(4) << (now->tm_year + 1900) << '-' << std::setw(2) << (now->tm_mon + 1) << '-' << std::setw(2) << now->tm_mday
		  << ' ' << std::setw(2) << now->tm_hour << ':' << std::setw(2) << now->tm_min << ':' << std::setw(2) << now->tm_sec
		  << " [" << Logger::getLogLevelStr(level) << "] ";
		s.fill(prevfill);
		return s;
	}
};

struct RSyslogLogger : public LoggingStream
{
	std::ostream& startLog(int level) override
	{
		if (level == Logger::DEBUG) return _debug;
		if (level == Logger::INFO) return _info;
		if (level == Logger::WARNING) return _warn;
		if (level == Logger::ERROR) return _error;
		if (level == Logger::CRITICAL) return _critical;
		return _nullStream;
	}

	explicit RSyslogLogger(const char* name)
		: _debugb(LOG_DEBUG), _infob(LOG_INFO), _warnb(LOG_WARNING), _errorb(LOG_ERR), _criticalb(LOG_CRIT)
		, _debug(&_debugb), _info(&_infob), _warn(&_warnb), _error(&_errorb), _critical(&_criticalb)
	{
		openlog(name, LOG_CONS | LOG_PID | LOG_NDELAY, LOG_USER);
	}

	~RSyslogLogger()
	{
		closelog();
	}

	RSyslogBuf _debugb;
	RSyslogBuf _infob;
	RSyslogBuf _warnb;
	RSyslogBuf _errorb;
	RSyslogBuf _criticalb;
	std::ostream _debug;
	std::ostream _info;
	std::ostream _warn;
	std::ostream _error;
	std::ostream _critical;
	NullStream _nullStream;
};