#include "shm_host.h"

#if defined(_WIN32)

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <windows.h>

#include <music_router/control.h>

namespace mr
{

namespace
{

constexpr std::uint32_t kSlotsOffset = 256;

std::size_t segment_bytes(std::uint32_t capacity)
{
    return kSlotsOffset + static_cast<std::size_t>(capacity) * sizeof(MrEvent);
}

}

std::unique_ptr<HostSegment> HostSegment::create(std::uint32_t capacity, std::uint32_t sample_rate,
                                                 std::uint32_t max_block, std::uint32_t lookahead)
{
    char shm_name[MR_SHM_NAME_MAX];
    std::snprintf(shm_name, sizeof(shm_name), "/mr-%lu",
                  static_cast<unsigned long>(::GetCurrentProcessId()));

    const std::size_t bytes = segment_bytes(capacity);
    const std::uint64_t width = static_cast<std::uint64_t>(bytes);
    HANDLE mapping = ::CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                          static_cast<DWORD>((width >> 32) & 0xFFFFFFFFu),
                                          static_cast<DWORD>(width & 0xFFFFFFFFu), shm_name);
    if (mapping == nullptr)
    {
        std::fprintf(stderr, "[clap-ipc] CreateFileMapping failed (%lu)\n", ::GetLastError());
        return nullptr;
    }

    void* base = ::MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
    if (base == nullptr)
    {
        std::fprintf(stderr, "[clap-ipc] MapViewOfFile failed (%lu)\n", ::GetLastError());
        ::CloseHandle(mapping);
        return nullptr;
    }

    auto* header = static_cast<MrRingHeader*>(base);
    std::memset(base, 0, sizeof(MrRingHeader));
    header->magic = MR_RING_MAGIC;
    header->proto_version = MR_PROTO_VERSION;
    header->capacity = capacity;
    header->slot_size = sizeof(MrEvent);
    header->sample_rate = sample_rate;
    header->max_block = max_block;
    header->lookahead_frames = lookahead;
    header->slots_offset = kSlotsOffset;
    header->head = 0;
    header->tail = 0;
    header->stream_frame = 0;

    auto segment = std::unique_ptr<HostSegment>(new HostSegment());
    segment->name_ = shm_name;
    segment->base_ = base;
    segment->bytes_ = bytes;
    segment->handle_ = reinterpret_cast<std::intptr_t>(mapping);
    segment->capacity_ = capacity;
    segment->sample_rate_ = sample_rate;
    segment->max_block_ = max_block;
    segment->lookahead_ = lookahead;
    return segment;
}

HostSegment::~HostSegment()
{
    if (base_ != nullptr)
    {
        ::UnmapViewOfFile(base_);
    }
    if (handle_ != -1 && handle_ != 0)
    {
        ::CloseHandle(reinterpret_cast<HANDLE>(handle_));
    }
}

}

#endif
