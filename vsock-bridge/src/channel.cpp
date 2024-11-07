#include <channel.h>

namespace vsockio
{
    void DirectChannel::performIO()
    {
        // Try reading from and writing to both sockets.
        // This is less efficient, but keeps the logic simple.

        _a->readInput();
        _b->readInput();
        _a->writeOutput();
        _b->writeOutput();
    }

}