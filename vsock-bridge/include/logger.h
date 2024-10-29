#pragma once

#include <chrono>
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

	template <typename... Ts>
	void Log(int level, const Ts&... args)
	{
		if (level < _minLevel || _streamProvider == nullptr) return;
		std::lock_guard<std::mutex> lk(_lock);
		auto& s = _streamProvider->startLog(level);
        (s << ... << args);
        s << std::endl;
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

struct PerfLogger
{
    const char* const _name;
    const std::chrono::time_point<std::chrono::steady_clock> _start;

    explicit PerfLogger(const char* name) : _name(name), _start(std::chrono::steady_clock::now()) {}
    ~PerfLogger()
    {
        const auto end = std::chrono::steady_clock::now();
        const std::chrono::duration<double> diff = end - _start;
        Logger::instance->Log(Logger::DEBUG, "Latency ", _name, " ", diff.count(), "s");
    }
};

#ifdef ENABLE_VSOCKIO_PERF
#define VSOCKIO_COMBINE1(X,Y) X##Y
#define VSOCKIO_COMBINE(X,Y) VSOCKIO_COMBINE1(X,Y)
#define PERF_LOG(name) PerfLogger VSOCKIO_COMBINE(__perfLog, __LINE__){name}
#else
#define PERF_LOG(name) do {} while(0)
#endif
