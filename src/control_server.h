#pragma once

#include <cstdint>
#include <vector>

#include "config.h"

namespace mr
{

class AudioGraph;
class HostSegment;
class ICaptureBackend;
class NotificationCenter;

class ControlServer
{
public:
    ControlServer(AudioGraph& graph, HostSegment& segment, ICaptureBackend* capture,
                  NotificationCenter& center, const HostConfig& config);

    bool serve();

private:
    bool dispatch(int client_fd, const std::vector<std::uint8_t>& payload);
    void handle_hello(int client_fd, std::uint32_t request_id, const std::uint8_t* body, std::size_t len);
    void handle_get_params(int client_fd, std::uint32_t request_id, const std::uint8_t* body,
                           std::size_t len);
    void reply(int client_fd, std::uint16_t type, std::uint32_t request_id, const void* body,
               std::size_t body_len);

    AudioGraph& graph_;
    HostSegment& segment_;
    ICaptureBackend* capture_;
    NotificationCenter& center_;
    HostConfig config_;
    bool greeted_ = false;
};

}
