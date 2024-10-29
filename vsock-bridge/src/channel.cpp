#include <channel.h>

namespace vsockio
{
    void DirectChannel::performIO()
    {
        _a->readInput();
        _b->readInput();
        _a->writeOutput();
        _b->writeOutput();
    }

}