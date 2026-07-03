#include "rt_policy.h"

#if defined(_WIN32)

#include <windows.h>

#include <avrt.h>

namespace mr
{

namespace
{

class WinRtPolicy : public IRtPolicy
{
public:
    ~WinRtPolicy() override
    {
        if (task_ != nullptr)
        {
            AvRevertMmThreadCharacteristics(task_);
        }
    }

    bool elevate(std::thread::native_handle_type) override
    {
        DWORD index = 0;
        task_ = AvSetMmThreadCharacteristicsW(L"Pro Audio", &index);
        return task_ != nullptr;
    }

private:
    HANDLE task_ = nullptr;
};

}

std::unique_ptr<IRtPolicy> make_platform_rt_policy()
{
    return std::make_unique<WinRtPolicy>();
}

}

#endif
