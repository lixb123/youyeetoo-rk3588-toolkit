#include "worker_channel.h"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

namespace youyeetoo {

bool CreateChannelPair(int* parent_fd, int* child_fd) {
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds) < 0) {
        std::cerr << "[WorkerChannel] socketpair: " << std::strerror(errno) << "\n";
        return false;
    }
    *parent_fd = fds[0];
    *child_fd  = fds[1];
    return true;
}

WorkerChannel::WorkerChannel(int fd) : fd_(fd) {}

WorkerChannel::~WorkerChannel() { Close(); }

void WorkerChannel::Close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool WorkerChannel::Send(const std::string& msg) {
    if (fd_ < 0) return false;
    if (msg.size() > kWorkerMaxMessageSize) {
        std::cerr << "[WorkerChannel] message too large: " << msg.size() << " bytes\n";
        return false;
    }
    const ssize_t n = ::send(fd_, msg.data(), msg.size(), MSG_NOSIGNAL);
    if (n != static_cast<ssize_t>(msg.size())) {
        if (errno != EPIPE && errno != ECONNRESET) {
            std::cerr << "[WorkerChannel] send: " << std::strerror(errno) << "\n";
        }
        return false;
    }
    return true;
}

bool WorkerChannel::TryRecv(std::string* msg, bool* error) {
    if (error) *error = false;
    if (fd_ < 0) {
        if (error) *error = true;
        return false;
    }

    struct pollfd pfd{};
    pfd.fd     = fd_;
    pfd.events = POLLIN;
    const int ret = ::poll(&pfd, 1, 0);  // non-blocking
    if (ret < 0) {
        if (error) *error = true;
        return false;
    }
    if (ret == 0) return false;  // nothing available

    if (pfd.revents & (POLLHUP | POLLERR)) {
        if (error) *error = true;
        return false;
    }

    char buf[kWorkerMaxMessageSize];
    const ssize_t n = ::recv(fd_, buf, sizeof(buf), MSG_DONTWAIT);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return false;
        if (error) *error = true;
        return false;
    }
    if (n == 0) {
        if (error) *error = true;  // peer closed
        return false;
    }

    msg->assign(buf, static_cast<size_t>(n));
    return true;
}

}  // namespace youyeetoo
