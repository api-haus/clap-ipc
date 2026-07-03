#include "audio_backend.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "config.h"
#include "invariants.h"

namespace mr
{

namespace
{

void write_u32(std::FILE* f, std::uint32_t v)
{
    unsigned char b[4] = {static_cast<unsigned char>(v & 0xFF),
                          static_cast<unsigned char>((v >> 8) & 0xFF),
                          static_cast<unsigned char>((v >> 16) & 0xFF),
                          static_cast<unsigned char>((v >> 24) & 0xFF)};
    std::fwrite(b, 1, 4, f);
}

void write_u16(std::FILE* f, std::uint16_t v)
{
    unsigned char b[2] = {static_cast<unsigned char>(v & 0xFF),
                          static_cast<unsigned char>((v >> 8) & 0xFF)};
    std::fwrite(b, 1, 2, f);
}

class WavWriter
{
public:
    WavWriter(const char* path, std::uint32_t sample_rate, std::uint32_t channels)
        : channels_{channels}
    {
        file_ = std::fopen(path, "wb");
        if (file_ == nullptr)
        {
            return;
        }
        std::fwrite("RIFF", 1, 4, file_);
        write_u32(file_, 0);
        std::fwrite("WAVE", 1, 4, file_);
        std::fwrite("fmt ", 1, 4, file_);
        write_u32(file_, 16);
        write_u16(file_, 1);
        write_u16(file_, static_cast<std::uint16_t>(channels));
        write_u32(file_, sample_rate);
        write_u32(file_, sample_rate * channels * 2);
        write_u16(file_, static_cast<std::uint16_t>(channels * 2));
        write_u16(file_, 16);
        std::fwrite("data", 1, 4, file_);
        write_u32(file_, 0);
    }

    ~WavWriter()
    {
        if (file_ != nullptr)
        {
            std::fclose(file_);
        }
    }

    bool ok() const { return file_ != nullptr; }

    void write_planar(float* const* channels, std::uint32_t count, std::uint32_t frames)
    {
        for (std::uint32_t i = 0; i < frames; ++i)
        {
            for (std::uint32_t ch = 0; ch < channels_; ++ch)
            {
                float s = ch < count ? channels[ch][i] : 0.0f;
                if (s > 1.0f) s = 1.0f;
                if (s < -1.0f) s = -1.0f;
                std::int16_t q = static_cast<std::int16_t>(s * 32767.0f);
                write_u16(file_, static_cast<std::uint16_t>(q));
            }
        }
        data_bytes_ += frames * channels_ * 2;
    }

    void finish()
    {
        if (file_ == nullptr)
        {
            return;
        }
        std::fseek(file_, 4, SEEK_SET);
        write_u32(file_, 36 + data_bytes_);
        std::fseek(file_, 40, SEEK_SET);
        write_u32(file_, data_bytes_);
        std::fflush(file_);
    }

private:
    std::FILE* file_ = nullptr;
    std::uint32_t channels_;
    std::uint32_t data_bytes_ = 0;
};

class CaptureBackend : public ICaptureBackend
{
public:
    bool start(RenderFn render, std::uint32_t sample_rate, std::uint32_t max_block, IRtPolicy&) override
    {
        render_ = std::move(render);
        sample_rate_ = sample_rate;
        max_block_ = max_block;
        started_ = true;
        return true;
    }

    void stop() override { started_ = false; }

    bool is_offline() const override { return true; }

    std::uint32_t output_latency_frames() const override { return 0; }

    std::uint32_t render_offline(std::uint32_t blocks, const char* wav_path) override
    {
        if (!started_ || !render_)
        {
            return 0;
        }

        const std::uint32_t channels = kMaxChannels;
        std::vector<float> storage(static_cast<std::size_t>(channels) * max_block_, 0.0f);
        float* out[kMaxChannels];
        for (std::uint32_t ch = 0; ch < channels; ++ch)
        {
            out[ch] = storage.data() + static_cast<std::size_t>(ch) * max_block_;
        }

        WavWriter wav(wav_path, sample_rate_, channels);
        if (!wav.ok())
        {
            return 0;
        }

        ScopedThreadRole audio(ThreadRole::Audio);
        std::uint64_t stream_frame = 0;
        for (std::uint32_t b = 0; b < blocks; ++b)
        {
            render_(out, channels, max_block_, RenderClock{stream_frame, steady_now_ns(), 0u});
            wav.write_planar(out, channels, max_block_);
            stream_frame += max_block_;
        }
        wav.finish();
        return blocks * max_block_;
    }

private:
    RenderFn render_;
    std::uint32_t sample_rate_ = kDefaultSampleRate;
    std::uint32_t max_block_ = kDefaultBlock;
    bool started_ = false;
};

}

std::unique_ptr<ICaptureBackend> make_capture_backend()
{
    return std::make_unique<CaptureBackend>();
}

}
