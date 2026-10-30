#include "cpnetflux/core/io/rdma_data_socket.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <netdb.h>
#include <poll.h>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if CPNETFLUX_HAS_RDMA
#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>
#endif

namespace cpnetflux::core::io {
namespace {

common::Status unavailable() {
    return common::Status::runtimeError(
        "RDMA data transport is unavailable in this build (enable CPNETFLUX_ENABLE_RDMA)");
}

#if CPNETFLUX_HAS_RDMA

constexpr std::size_t kChunkSize = 1U << 20;
constexpr int kDefaultTimeoutMs = 30000;

int timeoutMs(SocketDeadline deadline) {
    if (!deadline) return kDefaultTimeoutMs;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        *deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) return 1;
    return static_cast<int>(std::min<std::int64_t>(remaining.count(), 30000));
}

common::Status rdmaStatus(const char* operation, int errorNumber = errno) {
    return common::Status::transportError(
        std::string(operation) + ": " + std::strerror(errorNumber), errorNumber);
}

common::Result<sockaddr_storage> resolveAddress(const char* host, std::uint16_t port,
                                                bool passive) {
    if (host == nullptr || *host == '\0') {
        return common::Status::invalidArgument("RDMA address must not be empty");
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = passive ? AI_PASSIVE : 0;
    addrinfo* results = nullptr;
    const std::string service = std::to_string(port);
    const int result = ::getaddrinfo(host, service.c_str(), &hints, &results);
    if (result != 0) {
        return common::Status::invalidArgument("RDMA address resolution failed: " +
                                               std::string(::gai_strerror(result)));
    }
    sockaddr_storage address{};
    if (results->ai_addrlen > sizeof(address)) {
        ::freeaddrinfo(results);
        return common::Status::invalidArgument("RDMA address is too large");
    }
    std::memcpy(&address, results->ai_addr, results->ai_addrlen);
    ::freeaddrinfo(results);
    return address;
}

common::Result<sockaddr_storage> resolveLocalAddress(std::string_view address) {
    if (address.empty()) return sockaddr_storage{};
    return resolveAddress(std::string(address).c_str(), 0, false);
}

common::Status waitEvent(rdma_event_channel* channel, rdma_cm_event_type expected,
                         SocketDeadline deadline, rdma_cm_event** result) {
    if (channel == nullptr || result == nullptr) {
        return common::Status::invalidArgument("invalid RDMA CM event wait");
    }
    pollfd descriptor{channel->fd, POLLIN, 0};
    int wait = timeoutMs(deadline);
    while (true) {
        const int ready = ::poll(&descriptor, 1, wait);
        if (ready < 0 && errno == EINTR) continue;
        if (ready == 0) return common::Status::transportError("RDMA CM event timed out");
        if (ready < 0) return rdmaStatus("poll RDMA CM event");
        if (!(descriptor.revents & POLLIN)) return common::Status::transportError(
            "RDMA CM event channel closed");
        if (::rdma_get_cm_event(channel, result) != 0) return rdmaStatus("rdma_get_cm_event");
        if ((*result)->event != expected || (*result)->status != 0) {
            const int status = (*result)->status;
            const auto event = (*result)->event;
            (void)::rdma_ack_cm_event(*result);
            return common::Status::transportError(
                "unexpected RDMA CM event " + std::to_string(static_cast<int>(event)) +
                " status=" + std::to_string(status), status);
        }
        return common::Status::ok();
    }
}

#endif

}  // namespace

#if CPNETFLUX_HAS_RDMA
struct RdmaDataSocket::Impl {
    rdma_event_channel* channel = nullptr;
    rdma_cm_id* id = nullptr;
    ibv_pd* pd = nullptr;
    ibv_cq* sendCq = nullptr;
    ibv_cq* recvCq = nullptr;
    ibv_mr* recvMr = nullptr;
    ibv_mr* sendMr = nullptr;
    std::vector<std::uint8_t> recvBuffer;
    std::vector<std::uint8_t> sendBuffer;
    std::size_t received = 0;
    std::size_t receiveOffset = 0;
    bool receivePosted = false;
    bool closed = false;
    std::atomic_bool disconnected{false};

    ~Impl() { close(); }

    void close() noexcept {
        if (closed) return;
        closed = true;
        if (id != nullptr && !disconnected.exchange(true)) (void)::rdma_disconnect(id);
        if (id != nullptr && id->qp != nullptr) ::rdma_destroy_qp(id);
        if (recvMr != nullptr) (void)::ibv_dereg_mr(recvMr);
        if (sendMr != nullptr) (void)::ibv_dereg_mr(sendMr);
        if (sendCq != nullptr) (void)::ibv_destroy_cq(sendCq);
        if (recvCq != nullptr) (void)::ibv_destroy_cq(recvCq);
        if (pd != nullptr) (void)::ibv_dealloc_pd(pd);
        if (id != nullptr) (void)::rdma_destroy_id(id);
        if (channel != nullptr) ::rdma_destroy_event_channel(channel);
        channel = nullptr;
        id = nullptr;
        pd = nullptr;
        sendCq = nullptr;
        recvCq = nullptr;
        recvMr = nullptr;
    }

    common::Status initializeQueuePair() {
        if (id == nullptr || id->verbs == nullptr) return common::Status::runtimeError(
            "RDMA connection has no verbs context");
        pd = ::ibv_alloc_pd(id->verbs);
        if (pd == nullptr) return rdmaStatus("ibv_alloc_pd");
        sendCq = ::ibv_create_cq(id->verbs, 32, nullptr, nullptr, 0);
        recvCq = ::ibv_create_cq(id->verbs, 32, nullptr, nullptr, 0);
        if (sendCq == nullptr || recvCq == nullptr) return rdmaStatus("ibv_create_cq");
        ibv_qp_init_attr attributes{};
        attributes.send_cq = sendCq;
        attributes.recv_cq = recvCq;
        attributes.cap.max_send_wr = 16;
        attributes.cap.max_recv_wr = 16;
        attributes.cap.max_send_sge = 1;
        attributes.cap.max_recv_sge = 1;
        attributes.qp_type = IBV_QPT_RC;
        attributes.sq_sig_all = 0;
        if (::rdma_create_qp(id, pd, &attributes) != 0) return rdmaStatus("rdma_create_qp");
        recvBuffer.resize(kChunkSize);
        sendBuffer.resize(kChunkSize);
        recvMr = ::ibv_reg_mr(pd, recvBuffer.data(), recvBuffer.size(), IBV_ACCESS_LOCAL_WRITE);
        if (recvMr == nullptr) return rdmaStatus("ibv_reg_mr receive buffer");
        sendMr = ::ibv_reg_mr(pd, sendBuffer.data(), sendBuffer.size(), 0);
        if (sendMr == nullptr) return rdmaStatus("ibv_reg_mr send buffer");
        return postReceive();
    }

    common::Status postReceive() {
        ibv_sge scatter{reinterpret_cast<std::uintptr_t>(recvBuffer.data()),
                        static_cast<std::uint32_t>(recvBuffer.size()), recvMr->lkey};
        ibv_recv_wr workRequest{};
        workRequest.wr_id = 1;
        workRequest.sg_list = &scatter;
        workRequest.num_sge = 1;
        ibv_recv_wr* bad = nullptr;
        if (::ibv_post_recv(id->qp, &workRequest, &bad) != 0) return rdmaStatus("ibv_post_recv");
        receivePosted = true;
        return common::Status::ok();
    }

    common::Status pollCompletion(ibv_cq* cq, ibv_wc* completion) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        for (;;) {
            const int count = ::ibv_poll_cq(cq, 1, completion);
            if (count < 0) return common::Status::transportError("ibv_poll_cq failed");
            if (count == 1) {
                if (completion->status != IBV_WC_SUCCESS) {
                    return common::Status::transportError(
                        "RDMA work completion failed: " +
                        std::string(::ibv_wc_status_str(completion->status)), completion->status);
                }
                return common::Status::ok();
            }
            if (std::chrono::steady_clock::now() >= deadline)
                return common::Status::transportError("RDMA work completion timed out");
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }

    common::Status sendChunk(const std::uint8_t* data, std::size_t length) {
        std::memcpy(sendBuffer.data(), data, length);
        ibv_sge scatter{reinterpret_cast<std::uintptr_t>(sendBuffer.data()),
                        static_cast<std::uint32_t>(length), sendMr->lkey};
        ibv_send_wr workRequest{};
        workRequest.wr_id = 2;
        workRequest.opcode = IBV_WR_SEND;
        workRequest.send_flags = IBV_SEND_SIGNALED;
        workRequest.sg_list = &scatter;
        workRequest.num_sge = 1;
        ibv_send_wr* bad = nullptr;
        const int postResult = ::ibv_post_send(id->qp, &workRequest, &bad);
        ibv_wc completion{};
        return postResult == 0 ? pollCompletion(sendCq, &completion) : rdmaStatus("ibv_post_send");
    }
};

struct RdmaListener::Impl {
    rdma_event_channel* channel = nullptr;
    rdma_cm_id* id = nullptr;
    std::uint16_t port = 0;

    ~Impl() { close(); }

    void close() noexcept {
        if (id != nullptr) (void)::rdma_destroy_id(id);
        if (channel != nullptr) ::rdma_destroy_event_channel(channel);
        id = nullptr;
        channel = nullptr;
    }
};

#else
struct RdmaDataSocket::Impl {
    bool closed = false;
    void* id = nullptr;
};

struct RdmaListener::Impl {
    void* id = nullptr;
    std::uint16_t port = 0;
};
#endif

bool rdmaTransportAvailable() noexcept { return CPNETFLUX_HAS_RDMA != 0; }

RdmaDataSocket::RdmaDataSocket() = default;
RdmaDataSocket::~RdmaDataSocket() = default;
RdmaDataSocket::RdmaDataSocket(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
RdmaDataSocket::RdmaDataSocket(RdmaDataSocket&&) noexcept = default;
RdmaDataSocket& RdmaDataSocket::operator=(RdmaDataSocket&&) noexcept = default;

common::Result<RdmaDataSocket> RdmaDataSocket::connect(const char* host, std::uint16_t port,
                                                       SocketDeadline deadline,
                                                       std::string_view localBindAddress) {
#if !CPNETFLUX_HAS_RDMA
    (void)host; (void)port; (void)deadline; (void)localBindAddress;
    return unavailable();
#else
    auto destination = resolveAddress(host, port, false);
    if (!destination.isOk()) return destination.status();
    auto* channel = ::rdma_create_event_channel();
    if (channel == nullptr) return rdmaStatus("rdma_create_event_channel");
    rdma_cm_id* id = nullptr;
    if (::rdma_create_id(channel, &id, nullptr, RDMA_PS_TCP) != 0) {
        ::rdma_destroy_event_channel(channel);
        return rdmaStatus("rdma_create_id");
    }
    sockaddr_storage local{};
    sockaddr* source = nullptr;
    if (!localBindAddress.empty()) {
        auto resolved = resolveLocalAddress(localBindAddress);
        if (!resolved.isOk()) { ::rdma_destroy_id(id); ::rdma_destroy_event_channel(channel); return resolved.status(); }
        local = resolved.value();
        source = reinterpret_cast<sockaddr*>(&local);
    }
    if (::rdma_resolve_addr(id, source,
                            reinterpret_cast<sockaddr*>(&destination.value()),
                            timeoutMs(deadline)) != 0) {
        ::rdma_destroy_id(id); ::rdma_destroy_event_channel(channel);
        return rdmaStatus("rdma_resolve_addr");
    }
    rdma_cm_event* event = nullptr;
    auto status = waitEvent(channel, RDMA_CM_EVENT_ADDR_RESOLVED, deadline, &event);
    if (!status.isOk()) { ::rdma_destroy_id(id); ::rdma_destroy_event_channel(channel); return status; }
    (void)::rdma_ack_cm_event(event);
    if (::rdma_resolve_route(id, timeoutMs(deadline)) != 0) {
        ::rdma_destroy_id(id); ::rdma_destroy_event_channel(channel); return rdmaStatus("rdma_resolve_route");
    }
    status = waitEvent(channel, RDMA_CM_EVENT_ROUTE_RESOLVED, deadline, &event);
    if (!status.isOk()) { ::rdma_destroy_id(id); ::rdma_destroy_event_channel(channel); return status; }
    (void)::rdma_ack_cm_event(event);
    auto impl = std::make_unique<Impl>();
    impl->channel = channel;
    impl->id = id;
    status = impl->initializeQueuePair();
    if (!status.isOk()) return status;
    rdma_conn_param parameters{};
    parameters.initiator_depth = 1;
    parameters.responder_resources = 1;
    parameters.retry_count = 7;
    parameters.rnr_retry_count = 7;
    if (::rdma_connect(id, &parameters) != 0) return rdmaStatus("rdma_connect");
    status = waitEvent(channel, RDMA_CM_EVENT_ESTABLISHED, deadline, &event);
    if (!status.isOk()) return status;
    (void)::rdma_ack_cm_event(event);
    return RdmaDataSocket(std::move(impl));
#endif
}

bool RdmaDataSocket::valid() const noexcept {
    return impl_ != nullptr && !impl_->closed && impl_->id != nullptr;
}

void RdmaDataSocket::shutdown() noexcept {
#if CPNETFLUX_HAS_RDMA
    if (impl_ != nullptr && impl_->id != nullptr && !impl_->disconnected.exchange(true))
        (void)::rdma_disconnect(impl_->id);
#endif
}

common::Status RdmaDataSocket::writeAll(const std::uint8_t* data, std::size_t length) {
#if !CPNETFLUX_HAS_RDMA
    (void)data; (void)length; return unavailable();
#else
    if (!valid() || (length != 0 && data == nullptr)) return common::Status::invalidArgument("invalid RDMA write");
    while (length != 0) {
        const std::size_t chunk = std::min(length, kChunkSize);
        const auto status = impl_->sendChunk(data, chunk);
        if (!status.isOk()) return status;
        data += chunk;
        length -= chunk;
    }
    return common::Status::ok();
#endif
}

common::Result<std::size_t> RdmaDataSocket::readSome(std::uint8_t* data, std::size_t length) {
#if !CPNETFLUX_HAS_RDMA
    (void)data; (void)length; return unavailable();
#else
    if (!valid() || data == nullptr || length == 0) return common::Status::invalidArgument("invalid RDMA read");
    if (impl_->received == 0) {
        if (!impl_->receivePosted) {
            const auto posted = impl_->postReceive();
            if (!posted.isOk()) return posted;
        }
        ibv_wc completion{};
        const auto status = impl_->pollCompletion(impl_->recvCq, &completion);
        if (!status.isOk()) return status;
        impl_->receivePosted = false;
        impl_->received = completion.byte_len;
        impl_->receiveOffset = 0;
    }
    const std::size_t copied = std::min(length, impl_->received);
    std::memcpy(data, impl_->recvBuffer.data() + impl_->receiveOffset, copied);
    impl_->receiveOffset += copied;
    impl_->received -= copied;
    if (impl_->received == 0) {
        const auto posted = impl_->postReceive();
        if (!posted.isOk()) return posted;
    }
    return copied;
#endif
}

common::Status RdmaDataSocket::readAll(std::uint8_t* data, std::size_t length) {
    std::size_t completed = 0;
    while (completed < length) {
        auto result = readSome(data + completed, length - completed);
        if (!result.isOk()) return result.status();
        completed += result.value();
    }
    return common::Status::ok();
}

void RdmaDataSocket::close() noexcept {
#if CPNETFLUX_HAS_RDMA
    if (impl_ != nullptr) impl_->close();
#endif
    impl_.reset();
}

RdmaListener::RdmaListener() = default;
RdmaListener::~RdmaListener() = default;
RdmaListener::RdmaListener(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
RdmaListener::RdmaListener(RdmaListener&&) noexcept = default;
RdmaListener& RdmaListener::operator=(RdmaListener&&) noexcept = default;

common::Result<RdmaListener> RdmaListener::listen(const char* host, std::uint16_t port, int backlog) {
#if !CPNETFLUX_HAS_RDMA
    (void)host; (void)port; (void)backlog; return unavailable();
#else
    auto address = resolveAddress(host, port, true);
    if (!address.isOk()) return address.status();
    auto impl = std::make_unique<Impl>();
    impl->channel = ::rdma_create_event_channel();
    if (impl->channel == nullptr) return rdmaStatus("rdma_create_event_channel");
    if (::rdma_create_id(impl->channel, &impl->id, nullptr, RDMA_PS_TCP) != 0) return rdmaStatus("rdma_create_id");
    if (::rdma_bind_addr(impl->id, reinterpret_cast<sockaddr*>(&address.value())) != 0) return rdmaStatus("rdma_bind_addr");
    if (::rdma_listen(impl->id, backlog) != 0) return rdmaStatus("rdma_listen");
    impl->port = port;
    return RdmaListener(std::move(impl));
#endif
}

bool RdmaListener::valid() const noexcept { return impl_ != nullptr && impl_->id != nullptr; }
std::uint16_t RdmaListener::port() const noexcept { return impl_ == nullptr ? 0 : impl_->port; }

common::Result<RdmaDataSocket> RdmaListener::accept(SocketDeadline deadline) {
#if !CPNETFLUX_HAS_RDMA
    (void)deadline; return unavailable();
#else
    if (!valid()) return common::Status::invalidArgument("invalid RDMA listener");
    rdma_cm_event* event = nullptr;
    auto status = waitEvent(impl_->channel, RDMA_CM_EVENT_CONNECT_REQUEST, deadline, &event);
    if (!status.isOk()) return status;
    rdma_cm_id* id = event->id;
    (void)::rdma_ack_cm_event(event);
    auto* connectionChannel = ::rdma_create_event_channel();
    if (connectionChannel == nullptr) { (void)::rdma_destroy_id(id); return rdmaStatus("rdma_create_event_channel"); }
    if (::rdma_migrate_id(id, connectionChannel) != 0) {
        ::rdma_destroy_event_channel(connectionChannel); (void)::rdma_destroy_id(id);
        return rdmaStatus("rdma_migrate_id");
    }
    auto connection = std::make_unique<RdmaDataSocket::Impl>();
    connection->channel = connectionChannel;
    connection->id = id;
    status = connection->initializeQueuePair();
    if (!status.isOk()) return status;
    rdma_conn_param parameters{};
    parameters.initiator_depth = 1;
    parameters.responder_resources = 1;
    parameters.retry_count = 7;
    parameters.rnr_retry_count = 7;
    if (::rdma_accept(id, &parameters) != 0) return rdmaStatus("rdma_accept");
    status = waitEvent(connectionChannel, RDMA_CM_EVENT_ESTABLISHED, deadline, &event);
    if (!status.isOk()) return status;
    (void)::rdma_ack_cm_event(event);
    return RdmaDataSocket(std::move(connection));
#endif
}

void RdmaListener::close() noexcept {
#if CPNETFLUX_HAS_RDMA
    if (impl_ != nullptr) impl_->close();
#endif
    impl_.reset();
}

}  // namespace cpnetflux::core::io
