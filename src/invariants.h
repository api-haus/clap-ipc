#pragma once

#include <cassert>

namespace mr
{

enum class ThreadRole
{
    Unknown,
    Control,
    Audio
};

inline thread_local ThreadRole g_thread_role = ThreadRole::Unknown;

inline void set_thread_role(ThreadRole role) noexcept
{
    g_thread_role = role;
}

inline ThreadRole thread_role() noexcept
{
    return g_thread_role;
}

inline bool on_control_thread() noexcept
{
    return g_thread_role == ThreadRole::Control;
}

inline bool on_audio_thread() noexcept
{
    return g_thread_role == ThreadRole::Audio;
}

class ScopedThreadRole
{
public:
    explicit ScopedThreadRole(ThreadRole role) noexcept : previous_{g_thread_role}
    {
        g_thread_role = role;
    }

    ~ScopedThreadRole() noexcept
    {
        g_thread_role = previous_;
    }

    ScopedThreadRole(const ScopedThreadRole&) = delete;
    ScopedThreadRole& operator=(const ScopedThreadRole&) = delete;

private:
    ThreadRole previous_;
};

}

#define MR_ASSERT_CONTROL_THREAD() assert(::mr::on_control_thread() && "must run on the control thread")
#define MR_ASSERT_AUDIO_THREAD() assert(::mr::on_audio_thread() && "must run on the audio thread")
