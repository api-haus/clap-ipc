#include "rt_policy.h"

#include <cstdio>

#include <pthread.h>
#include <sched.h>

#include "config.h"

namespace mr
{

namespace
{

class LinuxRtPolicy : public IRtPolicy
{
public:
    bool elevate(std::thread::native_handle_type handle) override
    {
        const int max = sched_get_priority_max(SCHED_FIFO);
        const int min = sched_get_priority_min(SCHED_FIFO);
        sched_param param{};
        param.sched_priority =
            min + static_cast<int>((max - min) * kDefaultRtPriorityFraction);
        const int rc = pthread_setschedparam(static_cast<pthread_t>(handle), SCHED_FIFO, &param);
        if (rc != 0)
        {
            std::fprintf(stderr,
                         "[clap-ipc] SCHED_FIFO elevation failed (%d); running at normal priority\n",
                         rc);
            return false;
        }
        return true;
    }
};

}

std::unique_ptr<IRtPolicy> make_platform_rt_policy()
{
    return std::make_unique<LinuxRtPolicy>();
}

}
