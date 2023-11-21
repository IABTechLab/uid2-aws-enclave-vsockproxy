#include "version.h"
#include "vsock-bridge.h"

using namespace vsockio;
using namespace vsockproxy;

#define VSB_MAX_POLL_EVENTS 256

void sigpipe_handler(int unused)
{
    Logger::instance->Log(Logger::DEBUG, "SIGPIPE received");
}

std::vector<std::thread*> serviceThreads;

std::unique_ptr<Endpoint> createEndpoint(endpoint_scheme scheme, std::string address, uint16_t port)
{
    if (scheme == endpoint_scheme::TCP4)
    {
        return std::move(std::make_unique<TCP4Endpoint>(address, port));
    }
    else if (scheme == endpoint_scheme::VSOCK)
    {
        int cid = std::atoi(address.c_str());
        return std::move(std::make_unique<VSockEndpoint>(cid, port));
    }
    else
    {
        return nullptr;
    }
}

Listener* create_listener(std::vector<Dispatcher*>& dispatchers, endpoint_scheme inScheme, std::string inAddress, uint16_t inPort, endpoint_scheme outScheme, std::string outAddress, uint16_t outPort)
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
        return new Listener(std::move(listenEp), std::move(connectEp), dispatchers);
    }
}

void start_services(std::vector<service_description>& services, int numIOThreads, int numWorkers)
{
    Logger::instance->Log(Logger::INFO, "Starting ", numWorkers, " worker threads...");

    for (int i = 0; i < numWorkers; i++)
    {
        auto t = std::make_unique<WorkerThread>(
            /*init:*/ []() { 
                BufferManager::arena->init(512, 2000); 
            }
        );
        ThreadPool::threads.push_back(std::move(t));
    }

    for (auto& sd : services)
    {
        std::vector<Dispatcher*>* dispatchers = new std::vector<Dispatcher*>();
        for (int i = 0; i < 1; i++)
        {
            Dispatcher* d = new Dispatcher(i, new EpollPoller(VSB_MAX_POLL_EVENTS));
            dispatchers->push_back(d);
        }

        Logger::instance->Log(Logger::INFO, "Starting service: ", sd.name);
        Listener* listener = create_listener(
                            *dispatchers,
            /*inScheme:*/   sd.listen_ep.scheme,
            /*inAddress:*/  sd.listen_ep.address,
            /*inPort:*/     sd.listen_ep.port,
            /*outScheme:*/  sd.connect_ep.scheme,
            /*outAddress:*/ sd.connect_ep.address,
            /*outPort:*/    sd.connect_ep.port
        );

        if (listener == nullptr)
        {
            Logger::instance->Log(Logger::CRITICAL, "failed to start listener for ", sd.name);
            exit(1);
        }

        std::thread* listenerThread = new std::thread(&Listener::run, listener);
        std::thread* dispatcherThread = new std::thread(&Dispatcher::run, listener->_dispatchers[0]);
        serviceThreads.push_back(listenerThread);
    }

    for (auto* t : serviceThreads)
    {
        if (t->joinable())
            t->join();
    }
}

void show_help()
{
    std::cout
        << "usage: vsockpx -c <config-file> [-d] [--log-level [0-3]] [--num-threads n] [--iothreads n] [...]\n"
        << "  -c/--config: path to configuration file\n"
        << "  -d/--daemon: running in daemon mode\n"
        << "  --log-level: log level, 0=debug, 1=info, 2=warning, 3=error, 4=critical\n"
        << "  --iothreads: number of io threads, positive integer\n"
        << "  --workers: number of worker threads, positive integer\n"
        << std::flush;
}

void show_version()
{
	std::cout << VSOCK_BRIDGE_VERSION << std::endl;
}

void quit_bad_args(const char* reason, bool showhelp)
{
    std::cout << reason << std::endl;
    if (showhelp)
        show_help();
    exit(1);
}

int main(int argc, char* argv[])
{
    __sighandler_t sig = SIG_IGN;
    sigaction(SIGPIPE, (struct sigaction*)&sig, NULL);

    bool daemonize = false;
    std::string config_path;
    int min_log_level = 1;
    int num_worker_threads = 1;
    int num_iothreads = 1;

    if (argc < 2)
    {
        show_help();
        return 1;
    }

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            show_help();
            exit(0);
        }

		else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0)
		{
			show_version();
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
                quit_bad_args("no filepath followed by --config", false);
            }
            config_path = std::string(argv[++i]);
        }

        else if (strcmp(argv[i], "--workers") == 0)
        {
            if (i + 1 == argc)
            {
                quit_bad_args("no number followed by --workers", false);
            }

            num_worker_threads = std::stoi(std::string(argv[++i]));

            if (num_worker_threads == 0)
            {
                quit_bad_args("--workers should be at least 1", false);
            }
        }

        else if (strcmp(argv[i], "--log-level") == 0)
        {
            if (i + 1 == argc)
            {
                quit_bad_args("no log level followed by --log-level", false);
            }
            try
            {
                min_log_level = std::stoi(std::string(argv[++i]));
            }
            catch (std::invalid_argument _)
            {
                quit_bad_args("invalid log level, must be 0, 1, 2, 3 or 4", false);
            }
            if (min_log_level < 0 && min_log_level > 4)
            {
                quit_bad_args("invalid log level, must be 0, 1, 2, 3 or 4", false);
            }
        }

        else if (strcmp(argv[i], "--iothreads") == 0)
        {
            if (i + 1 == argc)
            {
                quit_bad_args("no number followed by --iothreads", false);
            }
            try
            {
                num_iothreads = std::stoi(std::string(argv[++i]));
            }
            catch (std::invalid_argument _)
            {
                quit_bad_args("invalid io thread count, must be number > 0", false);
            }
            if (min_log_level < 0 && min_log_level > 4)
            {
                quit_bad_args("invalid io thread count, must be number > 0", false);
            }
        }
    }

    if (config_path.empty())
    {
        quit_bad_args("no configuration file, use -c/--config or --help for more info.", false);
    }

    if (daemonize)
    {
        pid_t pid, sid;

        pid = fork();
        if (pid < 0) exit(1);
        if (pid > 0) exit(0); // exit parent process

        umask(0);

        Logger::instance->setMinLevel(min_log_level);
        Logger::instance->setStreamProvider(new RSyslogLogger("vsockpx"));

        sid = setsid();
        if (sid < 0) exit(1);

        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
    else
    {
        Logger::instance->setMinLevel(min_log_level);
        Logger::instance->setStreamProvider(new StdoutLogger());
    }

    std::vector<service_description> services = load_config(config_path);

    if (services.empty())
    {
        Logger::instance->Log(Logger::CRITICAL, "No services are configured, quitting.");
        exit(0);
    }

    start_services(services, num_iothreads, num_worker_threads);

    return 0;
}