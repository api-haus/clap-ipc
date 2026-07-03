#include "control_server.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <music_router/control.h>
#include <music_router/params.h>
#include <music_router/wire.h>

#include <clap/string-sizes.h>

#include "audio_backend.h"
#include "graph_swap.h"
#include "invariants.h"
#include "notification_center.h"
#include "plugin_node.h"
#include "shm_host.h"

namespace mr
{

namespace
{

constexpr std::uint32_t ST_OK = 0;
constexpr std::uint32_t ST_ERR_LOAD = 4;
constexpr std::uint32_t ST_ERR_NO_NODE = 5;
constexpr std::uint32_t ST_ERR_NO_TRACK = 6;
constexpr std::uint32_t ST_ERR_INVALID = 7;
constexpr std::uint32_t ST_ERR_VERSION = 3;

constexpr std::uint32_t kMaxFrame = 1u << 20;

bool read_exact(int fd, void* buf, std::size_t n)
{
    auto* p = static_cast<std::uint8_t*>(buf);
    std::size_t got = 0;
    while (got < n)
    {
        ssize_t r = ::read(fd, p + got, n - got);
        if (r <= 0)
        {
            return false;
        }
        got += static_cast<std::size_t>(r);
    }
    return true;
}

bool write_all(int fd, const void* buf, std::size_t n)
{
    auto* p = static_cast<const std::uint8_t*>(buf);
    std::size_t sent = 0;
    while (sent < n)
    {
        ssize_t r = ::write(fd, p + sent, n - sent);
        if (r <= 0)
        {
            return false;
        }
        sent += static_cast<std::size_t>(r);
    }
    return true;
}

std::uint32_t decode_u32(const std::uint8_t* b)
{
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
}

std::string take_path(const std::uint8_t* body, std::size_t body_len, std::size_t fixed,
                      std::uint32_t path_len)
{
    if (fixed + path_len > body_len)
    {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(body + fixed), path_len);
}

}

ControlServer::ControlServer(AudioGraph& graph, HostSegment& segment, ICaptureBackend* capture,
                             NotificationCenter& center, const HostConfig& config)
    : graph_{graph}, segment_{segment}, capture_{capture}, center_{center}, config_{config}
{
}

void ControlServer::reply(int client_fd, std::uint16_t type, std::uint32_t request_id,
                          const void* body, std::size_t body_len)
{
    MrMsgHeader header;
    header.type = type;
    header.flags = 0;
    header.request_id = request_id;

    const std::uint32_t total = static_cast<std::uint32_t>(sizeof(MrMsgHeader) + body_len);
    std::vector<std::uint8_t> frame(4 + total);
    frame[0] = static_cast<std::uint8_t>(total & 0xFF);
    frame[1] = static_cast<std::uint8_t>((total >> 8) & 0xFF);
    frame[2] = static_cast<std::uint8_t>((total >> 16) & 0xFF);
    frame[3] = static_cast<std::uint8_t>((total >> 24) & 0xFF);
    std::memcpy(frame.data() + 4, &header, sizeof(MrMsgHeader));
    if (body_len > 0)
    {
        std::memcpy(frame.data() + 4 + sizeof(MrMsgHeader), body, body_len);
    }
    write_all(client_fd, frame.data(), frame.size());
}

void ControlServer::handle_hello(int client_fd, std::uint32_t request_id, const std::uint8_t* body,
                                 std::size_t len)
{
    if (greeted_ || len < sizeof(MrHelloBody))
    {
        MrErrorBody err{ST_ERR_INVALID, 0};
        reply(client_fd, MR_MSG_ERROR, request_id, &err, sizeof(err));
        return;
    }
    MrHelloBody hello;
    std::memcpy(&hello, body, sizeof(hello));
    if (hello.magic != MR_RING_MAGIC || hello.proto_version != MR_PROTO_VERSION)
    {
        MrErrorBody err{ST_ERR_VERSION, 0};
        reply(client_fd, MR_MSG_ERROR, request_id, &err, sizeof(err));
        return;
    }

    MrWelcomeBody welcome;
    std::memset(&welcome, 0, sizeof(welcome));
    welcome.magic = MR_RING_MAGIC;
    welcome.proto_version = MR_PROTO_VERSION;
    std::snprintf(welcome.shm_name, sizeof(welcome.shm_name), "%s", segment_.name());
    welcome.capacity = segment_.capacity();
    welcome.slot_size = sizeof(MrEvent);
    welcome.sample_rate = segment_.sample_rate();
    welcome.max_block = segment_.max_block();
    welcome.lookahead_frames = segment_.lookahead();
    reply(client_fd, MR_MSG_WELCOME, request_id, &welcome, sizeof(welcome));
    greeted_ = true;
}

void ControlServer::handle_get_params(int client_fd, std::uint32_t request_id,
                                      const std::uint8_t* body, std::size_t len)
{
    MR_ASSERT_CONTROL_THREAD();
    MrGetParamsBody b{};
    if (len >= sizeof(b)) std::memcpy(&b, body, sizeof(b));

    std::vector<std::uint8_t> out(sizeof(MrParamListReply));
    MrParamListReply hdr{};
    hdr.track_id = b.track_id;
    hdr.dest_slot = b.dest_slot;
    hdr.start_index = b.start_index;

    PluginNode* node = graph_.find_node(b.track_id, b.dest_slot);
    if (node == nullptr)
    {
        hdr.status = ST_ERR_NO_NODE;
        std::memcpy(out.data(), &hdr, sizeof(hdr));
        reply(client_fd, MR_MSG_PARAM_LIST, request_id, out.data(), out.size());
        return;
    }

    const std::uint32_t total = node->param_count();
    const std::uint32_t maxc = (b.max_count == 0 || b.max_count > 128) ? 128u : b.max_count;
    const std::uint32_t end = std::min(total, b.start_index + maxc);
    const std::uint32_t begin = std::min(b.start_index, end);

    std::vector<MrParamInfo> recs;
    std::vector<std::pair<std::string, std::string>> strings;
    char name[CLAP_NAME_SIZE];
    char module[CLAP_PATH_SIZE];
    for (std::uint32_t i = begin; i < end; ++i)
    {
        MrParamInfo info{};
        name[0] = '\0';
        module[0] = '\0';
        node->param_info(i, &info, name, sizeof(name), module, sizeof(module));
        recs.push_back(info);
        strings.emplace_back(name, module);
    }

    hdr.status = ST_OK;
    hdr.total_count = total;
    hdr.returned_count = static_cast<std::uint32_t>(recs.size());
    std::memcpy(out.data(), &hdr, sizeof(hdr));

    const std::size_t recs_off = out.size();
    out.resize(recs_off + recs.size() * sizeof(MrParamInfo));
    if (!recs.empty())
    {
        std::memcpy(out.data() + recs_off, recs.data(), recs.size() * sizeof(MrParamInfo));
    }

    auto append_u32 = [&out](std::uint32_t v)
    {
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    };
    for (const std::pair<std::string, std::string>& s : strings)
    {
        append_u32(static_cast<std::uint32_t>(s.first.size()));
        out.insert(out.end(), s.first.begin(), s.first.end());
        append_u32(static_cast<std::uint32_t>(s.second.size()));
        out.insert(out.end(), s.second.begin(), s.second.end());
    }

    reply(client_fd, MR_MSG_PARAM_LIST, request_id, out.data(), out.size());
}

bool ControlServer::dispatch(int client_fd, const std::vector<std::uint8_t>& payload)
{
    MR_ASSERT_CONTROL_THREAD();
    MrMsgHeader header;
    std::memcpy(&header, payload.data(), sizeof(MrMsgHeader));
    const std::uint8_t* body = payload.data() + sizeof(MrMsgHeader);
    const std::size_t body_len = payload.size() - sizeof(MrMsgHeader);
    const std::uint32_t rid = header.request_id;

    switch (header.type)
    {
    case MR_MSG_HELLO:
        handle_hello(client_fd, rid, body, body_len);
        break;

    case MR_MSG_CREATE_TRACK:
    {
        std::uint32_t id = graph_.create_track();
        MrCreateTrackReply r{id == UINT32_MAX ? ST_ERR_INVALID : ST_OK,
                             id == UINT32_MAX ? 0u : id};
        reply(client_fd, MR_MSG_CREATE_TRACK, rid, &r, sizeof(r));
        break;
    }

    case MR_MSG_DESTROY_TRACK:
    {
        MrDestroyTrackBody b{};
        if (body_len >= sizeof(b)) std::memcpy(&b, body, sizeof(b));
        bool ok = graph_.destroy_track(b.track_id);
        MrStatusReply r{ok ? ST_OK : ST_ERR_NO_TRACK};
        reply(client_fd, MR_MSG_DESTROY_TRACK, rid, &r, sizeof(r));
        break;
    }

    case MR_MSG_LOAD_INSTRUMENT:
    {
        MrLoadInstrumentBody b{};
        std::memcpy(&b, body, sizeof(b) <= body_len ? sizeof(b) : body_len);
        std::string path = take_path(body, body_len, sizeof(b), b.path_len);
        MrLoadInstrumentReply r{ST_ERR_LOAD, 0, 0};
        auto node = PluginNode::create(path.c_str(), b.plugin_index, true, config_.sample_rate,
                                       config_.block, &center_);
        if (node)
        {
            r.out_channels = node->out_channels();
            r.note_dialect = node->note_dialect();
            r.status = graph_.set_track_instrument(b.track_id, std::move(node)) ? ST_OK
                                                                                : ST_ERR_NO_TRACK;
        }
        reply(client_fd, MR_MSG_LOAD_INSTRUMENT, rid, &r, sizeof(r));
        break;
    }

    case MR_MSG_INSERT_EFFECT:
    {
        MrInsertEffectBody b{};
        std::memcpy(&b, body, sizeof(b) <= body_len ? sizeof(b) : body_len);
        std::string path = take_path(body, body_len, sizeof(b), b.path_len);
        MrInsertEffectReply r{ST_ERR_LOAD, 0};
        auto node = PluginNode::create(path.c_str(), b.plugin_index, false, config_.sample_rate,
                                       config_.block, &center_);
        if (node)
        {
            r.out_channels = node->out_channels();
            r.status = graph_.insert_effect(b.track_id, b.slot_index, std::move(node))
                           ? ST_OK
                           : ST_ERR_NO_TRACK;
        }
        reply(client_fd, MR_MSG_INSERT_EFFECT, rid, &r, sizeof(r));
        break;
    }

    case MR_MSG_REMOVE_EFFECT:
    {
        MrRemoveEffectBody b{};
        if (body_len >= sizeof(b)) std::memcpy(&b, body, sizeof(b));
        bool ok = graph_.remove_effect(b.track_id, b.slot_index);
        MrStatusReply r{ok ? ST_OK : ST_ERR_NO_NODE};
        reply(client_fd, MR_MSG_REMOVE_EFFECT, rid, &r, sizeof(r));
        break;
    }

    case MR_MSG_SET_TRACK_GAIN:
    {
        MrSetTrackGainBody b{};
        if (body_len >= sizeof(b)) std::memcpy(&b, body, sizeof(b));
        bool ok = graph_.set_track_gain(b.track_id, b.gain);
        MrStatusReply r{ok ? ST_OK : ST_ERR_NO_TRACK};
        reply(client_fd, MR_MSG_SET_TRACK_GAIN, rid, &r, sizeof(r));
        break;
    }

    case MR_MSG_LOAD_STATE:
    {
        MrLoadStateBody b{};
        std::memcpy(&b, body, sizeof(b) <= body_len ? sizeof(b) : body_len);
        std::string path = take_path(body, body_len, sizeof(b), b.path_len);
        PluginNode* node = graph_.find_node(b.track_id, b.dest_slot);
        std::uint32_t status = ST_ERR_NO_NODE;
        if (node != nullptr)
        {
            if (node->load_state(path.c_str()))
            {
                status = ST_OK;
                center_.note_state_loaded(b.track_id, b.dest_slot);
            }
            else
            {
                status = ST_ERR_INVALID;
            }
        }
        MrStatusReply r{status};
        reply(client_fd, MR_MSG_LOAD_STATE, rid, &r, sizeof(r));
        break;
    }

    case MR_MSG_RENDER_CAPTURE:
    {
        MrRenderCaptureBody b{};
        std::memcpy(&b, body, sizeof(b) <= body_len ? sizeof(b) : body_len);
        std::string path = take_path(body, body_len, sizeof(b), b.out_path_len);
        MrRenderCaptureReply r{ST_ERR_INVALID, 0};
        if (capture_ != nullptr && !path.empty())
        {
            r.frames_written = capture_->render_offline(b.blocks, path.c_str());
            r.status = r.frames_written > 0 ? ST_OK : ST_ERR_INVALID;
        }
        reply(client_fd, MR_MSG_RENDER_CAPTURE, rid, &r, sizeof(r));
        break;
    }

    case MR_MSG_GET_PARAMS:
        handle_get_params(client_fd, rid, body, body_len);
        break;

    case MR_MSG_PARAM_VALUE_TEXT:
    {
        MrParamValueTextBody b{};
        if (body_len >= sizeof(b)) std::memcpy(&b, body, sizeof(b));
        PluginNode* node = graph_.find_node(b.track_id, b.dest_slot);
        char text[256];
        text[0] = '\0';
        std::uint32_t status = ST_ERR_NO_NODE;
        if (node != nullptr)
        {
            status = node->param_value_text(b.param_id, b.value, text, sizeof(text)) ? ST_OK
                                                                                     : ST_ERR_INVALID;
        }
        std::uint32_t text_len = status == ST_OK ? static_cast<std::uint32_t>(std::strlen(text)) : 0u;
        std::vector<std::uint8_t> out(sizeof(MrParamValueTextReply) + text_len);
        MrParamValueTextReply r{status, text_len};
        std::memcpy(out.data(), &r, sizeof(r));
        if (text_len > 0) std::memcpy(out.data() + sizeof(r), text, text_len);
        reply(client_fd, MR_MSG_PARAM_VALUE_TEXT, rid, out.data(), out.size());
        break;
    }

    case MR_MSG_POLL_NOTIFY:
    {
        MrPollNotifyBody b{};
        if (body_len >= sizeof(b)) std::memcpy(&b, body, sizeof(b));
        std::uint32_t maxc = (b.max_count == 0 || b.max_count > 256) ? 256u : b.max_count;
        std::vector<MrNotification> notes;
        MrNotification n;
        while (notes.size() < maxc && center_.poll(n))
        {
            notes.push_back(n);
        }
        std::vector<std::uint8_t> out(sizeof(MrPollNotifyReply) + notes.size() * sizeof(MrNotification));
        MrPollNotifyReply r{ST_OK, static_cast<std::uint32_t>(notes.size())};
        std::memcpy(out.data(), &r, sizeof(r));
        if (!notes.empty())
        {
            std::memcpy(out.data() + sizeof(r), notes.data(), notes.size() * sizeof(MrNotification));
        }
        reply(client_fd, MR_MSG_POLL_NOTIFY, rid, out.data(), out.size());
        break;
    }

    case MR_MSG_SHOW_GUI:
    {
        MrGuiBody b{};
        if (body_len >= sizeof(b)) std::memcpy(&b, body, sizeof(b));
        std::uint32_t status = ST_ERR_INVALID;
        if (config_.enable_gui)
        {
            PluginNode* node = graph_.find_node(b.track_id, b.dest_slot);
            status = node == nullptr ? ST_ERR_NO_NODE : (node->show_gui() ? ST_OK : ST_ERR_INVALID);
        }
        MrStatusReply r{status};
        reply(client_fd, MR_MSG_SHOW_GUI, rid, &r, sizeof(r));
        break;
    }

    case MR_MSG_HIDE_GUI:
    {
        MrGuiBody b{};
        if (body_len >= sizeof(b)) std::memcpy(&b, body, sizeof(b));
        std::uint32_t status = ST_ERR_INVALID;
        if (config_.enable_gui)
        {
            PluginNode* node = graph_.find_node(b.track_id, b.dest_slot);
            if (node != nullptr)
            {
                node->hide_gui();
                status = ST_OK;
            }
            else
            {
                status = ST_ERR_NO_NODE;
            }
        }
        MrStatusReply r{status};
        reply(client_fd, MR_MSG_HIDE_GUI, rid, &r, sizeof(r));
        break;
    }

    default:
    {
        MrErrorBody err{ST_ERR_INVALID, 0};
        reply(client_fd, MR_MSG_ERROR, rid, &err, sizeof(err));
        break;
    }
    }

    graph_.pump_main_thread();
    return true;
}

bool ControlServer::serve()
{
    MR_ASSERT_CONTROL_THREAD();
    if (config_.control_socket_path == nullptr)
    {
        std::fprintf(stderr, "[clap-ipc] no --control-socket path\n");
        return false;
    }

    int sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0)
    {
        std::perror("[clap-ipc] socket");
        return false;
    }

    ::unlink(config_.control_socket_path);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", config_.control_socket_path);
    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        std::perror("[clap-ipc] bind");
        ::close(sock);
        return false;
    }
    if (::listen(sock, 1) != 0)
    {
        std::perror("[clap-ipc] listen");
        ::close(sock);
        return false;
    }

    int client = ::accept(sock, nullptr, nullptr);
    if (client < 0)
    {
        std::perror("[clap-ipc] accept");
        ::close(sock);
        return false;
    }

    while (true)
    {
        std::uint8_t len_bytes[4];
        if (!read_exact(client, len_bytes, 4))
        {
            break;
        }
        std::uint32_t len = decode_u32(len_bytes);
        if (len < sizeof(MrMsgHeader) || len > kMaxFrame)
        {
            break;
        }
        std::vector<std::uint8_t> payload(len);
        if (!read_exact(client, payload.data(), len))
        {
            break;
        }
        if (!dispatch(client, payload))
        {
            break;
        }
    }

    ::close(client);
    ::close(sock);
    ::unlink(config_.control_socket_path);
    return true;
}

}
