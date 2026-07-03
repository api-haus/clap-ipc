#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#ifndef _WIN32
#include <csignal>
#include <pthread.h>
#endif

#include "audio_backend.h"
#include "config.h"
#include "control_server.h"
#include "graph_swap.h"
#include "invariants.h"
#include "notification_center.h"
#include "rt_policy.h"
#include "shm_host.h"
#include "track_render.h"

namespace
{

bool match(const char* arg, const char* key, const char*& value)
{
    std::size_t klen = std::strlen(key);
    if (std::strncmp(arg, key, klen) == 0 && arg[klen] == '=')
    {
        value = arg + klen + 1;
        return true;
    }
    return false;
}

mr::HostConfig parse_config(int argc, char** argv)
{
    mr::HostConfig cfg;
    const char* value = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        const char* a = argv[i];
        if (match(a, "--backend", value))
        {
            cfg.backend =
                std::strcmp(value, "device") == 0 ? mr::BackendKind::Device : mr::BackendKind::Capture;
        }
        else if (match(a, "--samplerate", value))
        {
            cfg.sample_rate = static_cast<std::uint32_t>(std::strtoul(value, nullptr, 10));
        }
        else if (match(a, "--block", value))
        {
            cfg.block = static_cast<std::uint32_t>(std::strtoul(value, nullptr, 10));
        }
        else if (match(a, "--capacity", value))
        {
            cfg.capacity = static_cast<std::uint32_t>(std::strtoul(value, nullptr, 10));
        }
        else if (match(a, "--lookahead", value))
        {
            cfg.lookahead_frames = static_cast<std::uint32_t>(std::strtoul(value, nullptr, 10));
        }
        else if (match(a, "--control-socket", value))
        {
            cfg.control_socket_path = value;
        }
        else if (match(a, "--device", value))
        {
            cfg.device = value;
        }
        else if (std::strcmp(a, "--drop-realtime") == 0)
        {
            cfg.drop_realtime = true;
        }
        else if (std::strcmp(a, "--enable-gui") == 0)
        {
            cfg.enable_gui = true;
        }
    }
    if (cfg.lookahead_frames == mr::kDefaultBlock * 2 && cfg.block != mr::kDefaultBlock)
    {
        cfg.lookahead_frames = cfg.block * 2;
    }
    return cfg;
}

#ifndef _WIN32
sigset_t term_signal_set()
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGINT);
    return set;
}

void install_term_handlers()
{
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = [](int) {};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
}
#endif

}

int main(int argc, char** argv)
{
    mr::set_thread_role(mr::ThreadRole::Control);

#ifndef _WIN32
    sigset_t term_set = term_signal_set();
    pthread_sigmask(SIG_BLOCK, &term_set, nullptr);
    install_term_handlers();
#endif

    mr::HostConfig cfg = parse_config(argc, argv);

    auto segment =
        mr::HostSegment::create(cfg.capacity, cfg.sample_rate, cfg.block, cfg.lookahead_frames);
    if (!segment)
    {
        return 1;
    }

    mr::AudioGraph graph;
    graph.set_offline(cfg.backend == mr::BackendKind::Capture);

    mr::NotificationCenter center;
    center.bind(&graph);

    mr::Renderer renderer(graph, segment->ring(), cfg.block, cfg.drop_realtime, center.echo_ring());
    mr::RenderFn render = [&renderer](float* const* out, std::uint32_t channels, std::uint32_t frames,
                                      const mr::RenderClock& clock)
    { renderer.render(out, channels, frames, clock); };

    std::unique_ptr<mr::IRtPolicy> rt;
    std::unique_ptr<mr::ICaptureBackend> capture;
    std::unique_ptr<mr::IAudioBackend> device;
    mr::IAudioBackend* backend = nullptr;
    mr::ICaptureBackend* capture_ptr = nullptr;

    if (cfg.backend == mr::BackendKind::Capture)
    {
        rt = std::make_unique<mr::NullRtPolicy>();
        capture = mr::make_capture_backend();
        capture_ptr = capture.get();
        backend = capture.get();
    }
    else
    {
#ifdef MR_HAVE_PORTAUDIO
        rt = mr::make_platform_rt_policy();
        device = mr::make_device_backend(cfg.device != nullptr ? std::string(cfg.device) : std::string{});
        backend = device.get();
#else
        std::fprintf(stderr, "[clap-ipc] device backend not built (no PortAudio)\n");
        return 1;
#endif
    }

    if (!backend->start(render, cfg.sample_rate, cfg.block, *rt))
    {
        std::fprintf(stderr, "[clap-ipc] backend start failed\n");
        return 1;
    }

    segment->set_output_latency_frames(backend->output_latency_frames());

#ifndef _WIN32
    pthread_sigmask(SIG_UNBLOCK, &term_set, nullptr);
#endif

    mr::ControlServer server(graph, *segment, capture_ptr, center, cfg);
    server.serve();

    backend->stop();
    graph.teardown();
    return 0;
}
