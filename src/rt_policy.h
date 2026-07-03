#pragma once

#include <memory>
#include <thread>

namespace mr
{

class IRtPolicy
{
public:
    virtual ~IRtPolicy() = default;
    virtual bool elevate(std::thread::native_handle_type handle) = 0;
};

class NullRtPolicy : public IRtPolicy
{
public:
    bool elevate(std::thread::native_handle_type) override { return true; }
};

std::unique_ptr<IRtPolicy> make_platform_rt_policy();

}
