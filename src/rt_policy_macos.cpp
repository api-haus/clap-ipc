#include "rt_policy.h"

#if defined(__APPLE__)

#include <pthread.h>
#include <pthread/qos.h>

namespace mr
{

namespace
{

class MacRtPolicy : public IRtPolicy
{
public:
    bool elevate(std::thread::native_handle_type) override
    {
        return pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) == 0;
    }
};

}

std::unique_ptr<IRtPolicy> make_platform_rt_policy()
{
    return std::make_unique<MacRtPolicy>();
}

}

#endif
