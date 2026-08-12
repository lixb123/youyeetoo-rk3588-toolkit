#pragma once

#include <cstddef>
#include <string>

namespace youyeetoo {

// ─── WorkerChannel ───────────────────────────────────────────────────────────
// Thin wrapper around a SOCK_SEQPACKET fd.
// SOCK_SEQPACKET preserves message boundaries → no length-header framing needed.
// Non-thread-safe. Each endpoint (parent / child) owns one fd of the pair.
// ─────────────────────────────────────────────────────────────────────────────

static constexpr size_t kWorkerMaxMessageSize = 65536;  // 64 KiB per message

class WorkerChannel {
public:
    explicit WorkerChannel(int fd);
    ~WorkerChannel();

    WorkerChannel(const WorkerChannel&) = delete;
    WorkerChannel& operator=(const WorkerChannel&) = delete;

    // Send a complete message. Returns false on broken channel or oversized message.
    bool Send(const std::string& msg);

    // Non-blocking receive.
    //   true  → *msg is populated with the received message.
    //   false + *error=false → no data ready (EAGAIN / nothing in queue).
    //   false + *error=true  → channel is broken/closed; caller should stop.
    bool TryRecv(std::string* msg, bool* error = nullptr);

    int  fd()    const { return fd_; }
    bool valid() const { return fd_ >= 0; }
    void Close();

private:
    int fd_;
};

// Create a connected SOCK_SEQPACKET pair.
// parent_fd stays in the parent process; child_fd is passed to the child.
bool CreateChannelPair(int* parent_fd, int* child_fd);

}  // namespace youyeetoo
