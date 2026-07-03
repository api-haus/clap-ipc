#include "shm_host.h"

#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

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
    std::snprintf(shm_name, sizeof(shm_name), "/mr-%d", static_cast<int>(::getpid()));

    int fd = ::shm_open(shm_name, O_CREAT | O_RDWR, 0600);
    if (fd < 0)
    {
        std::perror("[clap-ipc] shm_open");
        return nullptr;
    }

    const std::size_t bytes = segment_bytes(capacity);
    if (::ftruncate(fd, static_cast<off_t>(bytes)) != 0)
    {
        std::perror("[clap-ipc] ftruncate");
        ::close(fd);
        ::shm_unlink(shm_name);
        return nullptr;
    }

    void* base = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED)
    {
        std::perror("[clap-ipc] mmap");
        ::close(fd);
        ::shm_unlink(shm_name);
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
    segment->handle_ = fd;
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
        ::munmap(base_, bytes_);
    }
    if (handle_ >= 0)
    {
        ::close(static_cast<int>(handle_));
    }
    if (!name_.empty())
    {
        ::shm_unlink(name_.c_str());
    }
}

}
