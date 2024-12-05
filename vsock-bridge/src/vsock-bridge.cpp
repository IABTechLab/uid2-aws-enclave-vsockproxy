#include <vsock-bridge.h>

using namespace vsockio;
using namespace vsockproxy;

#define VSB_MAX_POLL_EVENTS 256

static void sigpipe_handler(int unused)
{
    Logger::instance->Log(Logger::DEBUG, "SIGPIPE received");
}

static std::unique_ptr<Endpoint> createEndpoint(EndpointScheme scheme, const std::string& address, uint16_t port)
{
    if (scheme == EndpointScheme::TCP4)
    {
        return std::move(std::make_unique<TCP4Endpoint>(address, port));
    }
    else if (scheme == EndpointScheme::VSOCK)
    {
        int cid = std::atoi(address.c_str());
        return std::move(std::make_unique<VSockEndpoint>(cid, port));
    }
    else
    {
        return nullptr;
    }
}

static std::unique_ptr<Listener> createListener(Dispatcher& dispatcher, EndpointScheme inScheme, const std::string& inAddress, uint16_t inPort, EndpointScheme outScheme, const std::string& outAddress, uint16_t outPort, int acceptRcvBuf, int acceptSndBuf, int peerRcvBuf, int peerSndBuf)
{
    auto listenEp { createEndpoint(inScheme, inAddress, inPort) };
    auto connectEp{ createEndpoint(outScheme, outAddress, outPort) };

    if (listenEp == nullptr)
    {
        Logger::instance->Log(Logger::ERROR, "invalid listen endpoint: ", inAddress, ":", inPort);
        return nullptr;
    }
    else if (connectEp == nullptr)
    {
        Logger::instance->Log(Logger::ERROR, "invalid connect endpoint: ", outAddress, ":", outPort);
        return nullptr;
    }
    else
    {
        return std::make_unique<Listener>(std::move(listenEp), std::move(connectEp), dispatcher, acceptRcvBuf, acceptSndBuf, peerRcvBuf, peerSndBuf);
    }
}

static void startServices(const std::vector<ServiceDescription>& services, int numWorkers)
{
    Logger::instance->Log(Logger::INFO, "Starting ", numWorkers, " worker threads...");

    EpollPollerFactory pollerFactory{VSB_MAX_POLL_EVENTS};
    IOThreadPool threadPool{(size_t)numWorkers, pollerFactory};
    Dispatcher dispatcher{threadPool};
    std::vector<std::unique_ptr<Listener>> listeners;
    std::vector<std::thread> listenerThreads;

    for (const auto& sd : services)
    {
        Logger::instance->Log(Logger::INFO, "Starting service: ", sd._name);
        auto listener = createListener(
                            dispatcher,
            /*inScheme:*/   sd._listenEndpoint._scheme,
            /*inAddress:*/  sd._listenEndpoint._address,
            /*inPort:*/     sd._listenEndpoint._port,
            /*outScheme:*/  sd._connectEndpoint._scheme,
            /*outAddress:*/ sd._connectEndpoint._address,
                            sd._acceptRcvBuf,
                            sd._acceptSndBuf,
                            sd._peerRcvBuf,
                            sd._peerSndBuf,
        );

        if (!listener)
        {
            Logger::instance->Log(Logger::CRITICAL, "failed to start listener for ", sd._name);
            exit(1);
        }

        listenerThreads.emplace_back(&Listener::run, listener.get());
        listeners.emplace_back(std::move(listener));
    }

    for (auto& t : listenerThreads)
    {
        if (t.joinable())
            t.join();
    }
}

static void showHelp()
{
    std::cout
        << "usage: vsockpx -c <config-file> [-d] [--log-level n] [--workers n] [...]\n"
        << "  -c/--config: path to configuration file\n"
        << "  -d/--daemon: running in daemon mode\n"
        << "  --log-level: log level, 0=debug, 1=info, 2=warning, 3=error, 4=critical (default: info)\n"
        << "  --workers: number of IO worker threads, positive integer (default: 1)\n"
        << std::flush;
}

static void quitBadArgs(const char* reason, bool showhelp)
{
    std::cout << reason << std::endl;
    if (showhelp)
        showHelp();
    exit(1);
}

int main(int argc, char* argv[])
{
    __sighandler_t sig = SIG_IGN;
    sigaction(SIGPIPE, (struct sigaction*)&sig, NULL);

    bool daemonize = false;
    std::string configPath;
    int minLogLevel = 1;
    int numWorkerThreads = 1;

    if (argc < 2)
    {
        showHelp();
        return 1;
    }

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            showHelp();
            exit(0);
        }

        else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--daemon") == 0)
        {
            daemonize = true;
        }

        else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0)
        {
            if (i + 1 == argc)
            {
                quitBadArgs("no filepath followed by --config", false);
            }
            configPath = std::string(argv[++i]);
        }

        else if (strcmp(argv[i], "--workers") == 0)
        {
            if (i + 1 == argc)
            {
                quitBadArgs("no number followed by --workers", false);
            }

            numWorkerThreads = std::stoi(std::string(argv[++i]));

            if (numWorkerThreads == 0)
            {
                quitBadArgs("--workers should be at least 1", false);
            }
        }

        else if (strcmp(argv[i], "--log-level") == 0)
        {
            if (i + 1 == argc)
            {
                quitBadArgs("no log level followed by --log-level", false);
            }
            try
            {
                minLogLevel = std::stoi(std::string(argv[++i]));
            }
            catch (std::invalid_argument _)
            {
                quitBadArgs("invalid log level, must be 0, 1, 2, 3 or 4", false);
            }
            if (minLogLevel < 0 || minLogLevel > 4)
            {
                quitBadArgs("invalid log level, must be 0, 1, 2, 3 or 4", false);
            }
        }
    }

    if (configPath.empty())
    {
        quitBadArgs("no configuration file, use -c/--config or --help for more info.", false);
    }

    if (daemonize)
    {
        pid_t pid, sid;

        pid = fork();
        if (pid < 0) exit(1);
        if (pid > 0) exit(0); // exit parent process

        umask(0);

        Logger::instance->setMinLevel(minLogLevel);
        Logger::instance->setStreamProvider(new RSyslogLogger("vsockpx"));

        sid = setsid();
        if (sid < 0) exit(1);

        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
    else
    {
        Logger::instance->setMinLevel(minLogLevel);
        Logger::instance->setStreamProvider(new StdoutLogger());
    }

    const std::vector<ServiceDescription> services = loadConfig(configPath);

    if (services.empty())
    {
        Logger::instance->Log(Logger::CRITICAL, "No services are configured, quitting.");
        exit(1);
    }

    startServices(services, numWorkerThreads);

    return 0;
}