#include "audio_backend.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <utility>

#if !defined(_WIN32)
#include <pthread.h>
#endif

#include <portaudio.h>

#include "config.h"
#include "invariants.h"

namespace mr
{

namespace
{

std::string to_lower(std::string s)
{
    for (char& c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

const char* host_api_name(PaHostApiIndex api)
{
    const PaHostApiInfo* info = Pa_GetHostApiInfo(api);
    return info != nullptr ? info->name : "?";
}

void log_devices(PaDeviceIndex chosen)
{
    PaDeviceIndex def = Pa_GetDefaultOutputDevice();
    std::fprintf(stderr, "[clap-ipc][audio] host APIs=%d devices=%d default-output=%d\n",
                 Pa_GetHostApiCount(), Pa_GetDeviceCount(), def);
    PaDeviceIndex count = Pa_GetDeviceCount();
    for (PaDeviceIndex i = 0; i < count; ++i)
    {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info == nullptr || info->maxOutputChannels <= 0)
        {
            continue;
        }
        std::fprintf(stderr, "[clap-ipc][audio]   [%d]%s%s api='%s' name='%s' out=%d rate=%.0f\n", i,
                     i == def ? " default" : "", i == chosen ? " <==OPEN" : "",
                     host_api_name(info->hostApi), info->name, info->maxOutputChannels,
                     info->defaultSampleRate);
    }
}

PaDeviceIndex select_device(const std::string& selector)
{
    if (selector.empty())
    {
        return Pa_GetDefaultOutputDevice();
    }

    bool all_digits = true;
    for (char c : selector)
    {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0)
        {
            all_digits = false;
            break;
        }
    }
    if (all_digits)
    {
        PaDeviceIndex idx = static_cast<PaDeviceIndex>(std::strtol(selector.c_str(), nullptr, 10));
        const PaDeviceInfo* info =
            idx >= 0 && idx < Pa_GetDeviceCount() ? Pa_GetDeviceInfo(idx) : nullptr;
        if (info != nullptr && info->maxOutputChannels > 0)
        {
            return idx;
        }
        std::fprintf(stderr, "[clap-ipc][audio] device index '%s' invalid, using default\n",
                     selector.c_str());
        return Pa_GetDefaultOutputDevice();
    }

    std::string needle = to_lower(selector);
    PaDeviceIndex count = Pa_GetDeviceCount();
    for (PaDeviceIndex i = 0; i < count; ++i)
    {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info == nullptr || info->maxOutputChannels <= 0)
        {
            continue;
        }
        if (to_lower(info->name).find(needle) != std::string::npos)
        {
            return i;
        }
    }
    std::fprintf(stderr, "[clap-ipc][audio] no output device matches '%s', using default\n",
                 selector.c_str());
    return Pa_GetDefaultOutputDevice();
}

class DeviceBackend : public IAudioBackend
{
public:
    explicit DeviceBackend(std::string selector) : selector_{std::move(selector)} {}
    ~DeviceBackend() override { stop(); }

    bool start(RenderFn render, std::uint32_t sample_rate, std::uint32_t max_block,
               IRtPolicy& rt) override
    {
        render_ = std::move(render);
        rt_ = &rt;
        sample_rate_ = sample_rate;
        stream_frame_ = 0;
        elevated_ = false;

        if (max_block < kMinDeviceBlock)
        {
            std::fprintf(stderr,
                         "[clap-ipc][audio] block %u is below the device minimum %u — small buffers stall "
                         "on this audio backend; refusing (the client should fall back to a larger block)\n",
                         max_block, kMinDeviceBlock);
            return false;
        }

        if (Pa_Initialize() != paNoError)
        {
            return false;
        }
        initialized_ = true;

        std::string selector = selector_;
        if (selector.empty())
        {
            const char* env = std::getenv("MR_AUDIO_DEVICE");
            if (env != nullptr)
            {
                selector = env;
            }
        }

        PaDeviceIndex device = select_device(selector);
        if (device == paNoDevice)
        {
            std::fprintf(stderr, "[clap-ipc][audio] no output device available\n");
            return false;
        }
        log_devices(device);

        const PaDeviceInfo* device_info = Pa_GetDeviceInfo(device);
        PaStreamParameters params{};
        params.device = device;
        params.channelCount = static_cast<int>(kMaxChannels);
        params.sampleFormat = paFloat32 | paNonInterleaved;
        params.suggestedLatency = device_info->defaultLowOutputLatency;
        params.hostApiSpecificStreamInfo = nullptr;

        PaError err = Pa_OpenStream(&stream_, nullptr, &params, static_cast<double>(sample_rate),
                                    max_block, paNoFlag, &DeviceBackend::callback, this);
        if (err != paNoError)
        {
            std::fprintf(stderr, "[clap-ipc][audio] Pa_OpenStream failed: %s\n",
                         Pa_GetErrorText(err));
            return false;
        }

        std::fprintf(stderr,
                     "[clap-ipc][audio] OPEN device=%d name='%s' api='%s' rate=%u block=%u "
                     "channels=%u latency=%.1fms\n",
                     device, device_info->name, host_api_name(device_info->hostApi), sample_rate,
                     max_block, kMaxChannels, params.suggestedLatency * 1000.0);

        err = Pa_StartStream(stream_);
        if (err != paNoError)
        {
            std::fprintf(stderr, "[clap-ipc][audio] Pa_StartStream failed: %s\n",
                         Pa_GetErrorText(err));
            return false;
        }

        const PaStreamInfo* stream_info = Pa_GetStreamInfo(stream_);
        const double actual_latency = stream_info != nullptr ? stream_info->outputLatency
                                                             : params.suggestedLatency;
        output_latency_frames_ =
            static_cast<std::uint32_t>(std::llround(actual_latency * static_cast<double>(sample_rate)));
        std::fprintf(stderr, "[clap-ipc][audio] output latency=%.2fms (%u frames @ %uHz) measured\n",
                     actual_latency * 1000.0, output_latency_frames_, sample_rate);

        monitor_run_.store(true, std::memory_order_release);
        monitor_ = std::thread(&DeviceBackend::monitor_loop, this);
        return true;
    }

    void stop() override
    {
        if (monitor_run_.exchange(false) && monitor_.joinable())
        {
            monitor_.join();
        }
        if (stream_ != nullptr)
        {
            Pa_StopStream(stream_);
            Pa_CloseStream(stream_);
            stream_ = nullptr;
        }
        if (initialized_)
        {
            Pa_Terminate();
            initialized_ = false;
        }
    }

    bool is_offline() const override { return false; }

    std::uint32_t output_latency_frames() const override { return output_latency_frames_; }

private:
    static int callback(const void*, void* output, unsigned long frames,
                        const PaStreamCallbackTimeInfo* time_info, PaStreamCallbackFlags, void* user)
    {
        auto* self = static_cast<DeviceBackend*>(user);
        if (!self->elevated_)
        {
            set_thread_role(ThreadRole::Audio);
#if defined(_WIN32)
            self->rt_->elevate(std::thread::native_handle_type{});
#else
            self->rt_->elevate(static_cast<std::thread::native_handle_type>(pthread_self()));
#endif
            self->elevated_ = true;
        }
        auto** out = static_cast<float**>(output);
        const std::uint32_t total = static_cast<std::uint32_t>(frames);
        RenderClock clock{self->stream_frame_, steady_now_ns(), self->output_latency_frames_};
        const double dac_latency = time_info->outputBufferDacTime - time_info->currentTime;
        if (dac_latency > 0.0)
        {
            clock.latency_frames = static_cast<std::uint32_t>(
                std::llround(dac_latency * static_cast<double>(self->sample_rate_)));
        }
        self->render_(out, kMaxChannels, total, clock);
        self->stream_frame_ += total;

        float peak = 0.0f;
        for (std::uint32_t ch = 0; ch < kMaxChannels; ++ch)
        {
            const float* samples = out[ch];
            for (std::uint32_t i = 0; i < total; ++i)
            {
                float a = std::fabs(samples[i]);
                if (a > peak)
                {
                    peak = a;
                }
            }
        }
        self->publish_peak(peak);
        self->callback_count_.fetch_add(1, std::memory_order_relaxed);
        self->frames_total_.fetch_add(total, std::memory_order_relaxed);
        return paContinue;
    }

    void publish_peak(float peak)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &peak, sizeof(bits));
        std::uint32_t prev = peak_bits_.load(std::memory_order_relaxed);
        while (bits > prev && !peak_bits_.compare_exchange_weak(prev, bits, std::memory_order_relaxed))
        {
        }
    }

    void monitor_loop()
    {
        std::uint64_t last_callbacks = 0;
        while (monitor_run_.load(std::memory_order_acquire))
        {
            for (int i = 0; i < 20 && monitor_run_.load(std::memory_order_acquire); ++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (!monitor_run_.load(std::memory_order_acquire))
            {
                break;
            }
            std::uint64_t callbacks = callback_count_.load(std::memory_order_relaxed);
            std::uint64_t frames = frames_total_.load(std::memory_order_relaxed);
            std::uint32_t bits = peak_bits_.exchange(0, std::memory_order_relaxed);
            float peak;
            std::memcpy(&peak, &bits, sizeof(peak));
            double dbfs = peak > 0.0f ? 20.0 * std::log10(static_cast<double>(peak)) : -240.0;
            std::fprintf(stderr,
                         "[clap-ipc][audio] callbacks=%llu (+%llu/s) frames=%llu peak=%.4f "
                         "(%.1f dBFS)\n",
                         static_cast<unsigned long long>(callbacks),
                         static_cast<unsigned long long>(callbacks - last_callbacks),
                         static_cast<unsigned long long>(frames), static_cast<double>(peak), dbfs);
            last_callbacks = callbacks;
        }
    }

    std::string selector_;
    RenderFn render_;
    IRtPolicy* rt_ = nullptr;
    PaStream* stream_ = nullptr;
    std::uint32_t sample_rate_ = 0;
    std::uint32_t output_latency_frames_ = 0;
    std::uint64_t stream_frame_ = 0;
    bool initialized_ = false;
    bool elevated_ = false;

    std::atomic<bool> monitor_run_{false};
    std::thread monitor_;
    std::atomic<std::uint64_t> callback_count_{0};
    std::atomic<std::uint64_t> frames_total_{0};
    std::atomic<std::uint32_t> peak_bits_{0};
};

}

std::unique_ptr<IAudioBackend> make_device_backend(std::string device_selector)
{
    return std::make_unique<DeviceBackend>(std::move(device_selector));
}

}
