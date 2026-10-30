#include "cpnetflux/core/io/rdma_data_socket.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <netdb.h>
#include <poll.h>
#include <span>
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

constexpr std::size_t kMessageBufferSize = kRdmaDataMessageBufferBytes;
constexpr std::size_t kSendQueueDepth = kRdmaDataQueueDepth;
constexpr std::size_t kReceiveQueueDepth = kRdmaDataReceiveQueueDepth;
constexpr std::size_t kCompletionQueueDepth = kReceiveQueueDepth * 2;
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
    struct SendSlot {
        std::vector<std::uint8_t> buffer;
        ibv_mr* memoryRegion = nullptr;
        bool inFlight = false;
    };

    struct ReceiveSlot {
        std::vector<std::uint8_t> buffer;
        ibv_mr* memoryRegion = nullptr;
        std::size_t received = 0;
        std::size_t offset = 0;
        bool posted = false;
    };

    rdma_event_channel* channel = nullptr;
    rdma_cm_id* id = nullptr;
    ibv_pd* pd = nullptr;
    ibv_cq* sendCq = nullptr;
    ibv_cq* recvCq = nullptr;
    std::array<SendSlot, kSendQueueDepth> sendSlots;
    std::array<ReceiveSlot, kReceiveQueueDepth> receiveSlots;
    std::size_t sendInFlight = 0;
    std::size_t activeReceiveSlot = kReceiveQueueDepth;
    RdmaTransportStats statistics;
    bool closed = false;
    std::atomic_bool disconnected{false};

    ~Impl() { close(); }

    void close() noexcept {
        if (closed) return;
        closed = true;
        if (id != nullptr && !disconnected.exchange(true)) (void)::rdma_disconnect(id);
        if (id != nullptr && id->qp != nullptr) ::rdma_destroy_qp(id);
        for (auto& slot : sendSlots) {
            if (slot.memoryRegion != nullptr) (void)::ibv_dereg_mr(slot.memoryRegion);
            slot.memoryRegion = nullptr;
        }
        for (auto& slot : receiveSlots) {
            if (slot.memoryRegion != nullptr) (void)::ibv_dereg_mr(slot.memoryRegion);
            slot.memoryRegion = nullptr;
        }
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
    }

    common::Status initializeQueuePair() {
        if (id == nullptr || id->verbs == nullptr) return common::Status::runtimeError(
            "RDMA connection has no verbs context");
        pd = ::ibv_alloc_pd(id->verbs);
        if (pd == nullptr) return rdmaStatus("ibv_alloc_pd");
        sendCq = ::ibv_create_cq(id->verbs, kCompletionQueueDepth, nullptr, nullptr, 0);
        recvCq = ::ibv_create_cq(id->verbs, kCompletionQueueDepth, nullptr, nullptr, 0);
        if (sendCq == nullptr || recvCq == nullptr) return rdmaStatus("ibv_create_cq");
        ibv_qp_init_attr attributes{};
        attributes.send_cq = sendCq;
        attributes.recv_cq = recvCq;
        attributes.cap.max_send_wr = static_cast<std::uint32_t>(kSendQueueDepth);
        attributes.cap.max_recv_wr = static_cast<std::uint32_t>(kReceiveQueueDepth);
        attributes.cap.max_send_sge = 1;
        attributes.cap.max_recv_sge = 1;
        attributes.qp_type = IBV_QPT_RC;
        attributes.sq_sig_all = 0;
        if (::rdma_create_qp(id, pd, &attributes) != 0) return rdmaStatus("rdma_create_qp");
        for (auto& slot : sendSlots) {
            slot.buffer.resize(kMessageBufferSize);
            slot.memoryRegion = ::ibv_reg_mr(pd, slot.buffer.data(), slot.buffer.size(), 0);
            if (slot.memoryRegion == nullptr) return rdmaStatus("ibv_reg_mr send buffer");
        }
        for (std::size_t index = 0; index < receiveSlots.size(); ++index) {
            auto& slot = receiveSlots[index];
            slot.buffer.resize(kMessageBufferSize);
            slot.memoryRegion = ::ibv_reg_mr(pd, slot.buffer.data(), slot.buffer.size(),
                                             IBV_ACCESS_LOCAL_WRITE);
            if (slot.memoryRegion == nullptr) return rdmaStatus("ibv_reg_mr receive buffer");
            const auto status = postReceive(index);
            if (!status.isOk()) return status;
        }
        return common::Status::ok();
    }

    common::Status postReceive(std::size_t index) {
        if (index >= receiveSlots.size())
            return common::Status::invalidArgument("invalid RDMA receive slot");
        auto& slot = receiveSlots[index];
        if (slot.memoryRegion == nullptr || slot.posted)
            return common::Status::runtimeError("RDMA receive slot is not reusable");
        ibv_sge scatter{reinterpret_cast<std::uintptr_t>(slot.buffer.data()),
                        static_cast<std::uint32_t>(slot.buffer.size()), slot.memoryRegion->lkey};
        ibv_recv_wr workRequest{};
        workRequest.wr_id = index;
        workRequest.sg_list = &scatter;
        workRequest.num_sge = 1;
        ibv_recv_wr* bad = nullptr;
        if (::ibv_post_recv(id->qp, &workRequest, &bad) != 0) return rdmaStatus("ibv_post_recv");
        slot.posted = true;
        return common::Status::ok();
    }

    common::Status pollCompletion(ibv_cq* cq, ibv_wc* completion) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        std::uint32_t emptyPolls = 0;
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
            if (++emptyPolls % 8192U == 0) std::this_thread::yield();
        }
    }

    common::Result<std::size_t> reapSendCompletions() {
        std::array<ibv_wc, kSendQueueDepth> completions{};
        ++statistics.sendCompletionPollCalls;
        const int count = ::ibv_poll_cq(sendCq, static_cast<int>(completions.size()),
                                        completions.data());
        if (count < 0) return common::Status::transportError("ibv_poll_cq send failed");
        if (count == 0) return std::size_t{0};
        ++statistics.sendCompletionBatches;
        common::Status firstError = common::Status::ok();
        for (int index = 0; index < count; ++index) {
            const auto& completion = completions[static_cast<std::size_t>(index)];
            if (completion.wr_id >= sendSlots.size()) {
                if (firstError.isOk()) firstError = common::Status::transportError(
                    "RDMA send completion has an invalid slot");
                continue;
            }
            auto& slot = sendSlots[static_cast<std::size_t>(completion.wr_id)];
            if (!slot.inFlight) {
                if (firstError.isOk()) firstError = common::Status::transportError(
                    "RDMA send completion used an idle slot");
                continue;
            }
            slot.inFlight = false;
            --sendInFlight;
            ++statistics.sendCompletions;
            if (completion.status != IBV_WC_SUCCESS && firstError.isOk()) {
                firstError = common::Status::transportError(
                    "RDMA work completion failed: " +
                    std::string(::ibv_wc_status_str(completion.status)), completion.status);
            }
        }
        if (!firstError.isOk()) return firstError;
        return static_cast<std::size_t>(count);
    }

    common::Status waitForSendProgress() {
        const auto waitStarted = std::chrono::steady_clock::now();
        ++statistics.sendCompletionWaitCount;
        std::uint32_t emptyPolls = 0;
        for (;;) {
            auto completions = reapSendCompletions();
            if (!completions.isOk()) {
                statistics.sendCompletionWaitNanoseconds += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - waitStarted).count());
                return completions.status();
            }
            if (completions.value() != 0) {
                statistics.sendCompletionWaitNanoseconds += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - waitStarted).count());
                return common::Status::ok();
            }
            if (++emptyPolls % 8192U == 0) std::this_thread::yield();
        }
    }

    common::Status waitForSendSlot(std::size_t* slotIndex) {
        if (slotIndex == nullptr)
            return common::Status::invalidArgument("missing RDMA send slot output");
        for (;;) {
            for (std::size_t index = 0; index < sendSlots.size(); ++index) {
                if (!sendSlots[index].inFlight) {
                    *slotIndex = index;
                    return common::Status::ok();
                }
            }
            const auto status = waitForSendProgress();
            if (!status.isOk()) return status;
        }
    }

    common::Status postSend(std::size_t slotIndex, std::size_t length) {
        auto& slot = sendSlots[slotIndex];
        ibv_sge scatter{reinterpret_cast<std::uintptr_t>(slot.buffer.data()),
                        static_cast<std::uint32_t>(length), slot.memoryRegion->lkey};
        ibv_send_wr workRequest{};
        workRequest.wr_id = slotIndex;
        workRequest.opcode = IBV_WR_SEND;
        workRequest.send_flags = IBV_SEND_SIGNALED;
        workRequest.sg_list = &scatter;
        workRequest.num_sge = 1;
        ibv_send_wr* bad = nullptr;
        if (::ibv_post_send(id->qp, &workRequest, &bad) != 0)
            return rdmaStatus("ibv_post_send");
        slot.inFlight = true;
        ++sendInFlight;
        ++statistics.sendWorkRequests;
        statistics.sendBytes += length;
        statistics.maxSendInFlight = std::max<std::uint64_t>(
            statistics.maxSendInFlight, sendInFlight);
        return common::Status::ok();
    }

    common::Status writeSpans(const std::array<std::span<const std::uint8_t>, 3>& spans) {
        std::size_t spanIndex = 0;
        std::size_t spanOffset = 0;
        std::size_t remaining = 0;
        for (const auto& span : spans) {
            if (span.size() > std::numeric_limits<std::size_t>::max() - remaining)
                return common::Status::invalidArgument("RDMA write length overflow");
            remaining += span.size();
        }
        while (remaining != 0) {
            std::size_t slotIndex = 0;
            const auto available = waitForSendSlot(&slotIndex);
            if (!available.isOk()) return available;
            auto& slot = sendSlots[slotIndex];
            const std::size_t length = std::min(remaining, slot.buffer.size());
            std::size_t copied = 0;
            while (copied < length) {
                while (spanIndex < spans.size() && spanOffset == spans[spanIndex].size()) {
                    ++spanIndex;
                    spanOffset = 0;
                }
                if (spanIndex >= spans.size())
                    return common::Status::runtimeError("RDMA span accounting underflow");
                const auto count = std::min(length - copied,
                                            spans[spanIndex].size() - spanOffset);
                std::memcpy(slot.buffer.data() + copied,
                            spans[spanIndex].data() + spanOffset, count);
                copied += count;
                spanOffset += count;
            }
            const auto posted = postSend(slotIndex, length);
            if (!posted.isOk()) return posted;
            remaining -= length;
        }
        return common::Status::ok();
    }

    common::Status drainSends() {
        while (sendInFlight != 0) {
            const auto status = waitForSendProgress();
            if (!status.isOk()) return status;
        }
        return common::Status::ok();
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
    return impl_->writeSpans({std::span<const std::uint8_t>(data, length), {}, {}});
#endif
}

common::Status RdmaDataSocket::writeFrame(const std::uint8_t* header,
                                          std::size_t headerLength,
                                          const std::uint8_t* prefix,
                                          std::size_t prefixLength,
                                          const std::uint8_t* payload,
                                          std::size_t payloadLength) {
#if !CPNETFLUX_HAS_RDMA
    (void)header; (void)headerLength; (void)prefix; (void)prefixLength;
    (void)payload; (void)payloadLength; return unavailable();
#else
    if (!valid() || (headerLength != 0 && header == nullptr) ||
        (prefixLength != 0 && prefix == nullptr) ||
        (payloadLength != 0 && payload == nullptr))
        return common::Status::invalidArgument("invalid RDMA frame spans");
    return impl_->writeSpans({std::span<const std::uint8_t>(header, headerLength),
                              std::span<const std::uint8_t>(prefix, prefixLength),
                              std::span<const std::uint8_t>(payload, payloadLength)});
#endif
}

common::Result<std::size_t> RdmaDataSocket::readSome(std::uint8_t* data, std::size_t length) {
#if !CPNETFLUX_HAS_RDMA
    (void)data; (void)length; return unavailable();
#else
    if (!valid() || data == nullptr || length == 0) return common::Status::invalidArgument("invalid RDMA read");
    const auto sendStatus = impl_->drainSends();
    if (!sendStatus.isOk()) return sendStatus;
    if (impl_->activeReceiveSlot == kReceiveQueueDepth) {
        ibv_wc completion{};
        const auto status = impl_->pollCompletion(impl_->recvCq, &completion);
        if (!status.isOk()) return status;
        if (completion.wr_id >= impl_->receiveSlots.size())
            return common::Status::transportError("RDMA completion has an invalid receive slot");
        impl_->activeReceiveSlot = static_cast<std::size_t>(completion.wr_id);
        auto& slot = impl_->receiveSlots[impl_->activeReceiveSlot];
        if (!slot.posted)
            return common::Status::transportError("RDMA completion used an unposted receive slot");
        slot.posted = false;
        slot.received = completion.byte_len;
        slot.offset = 0;
    }
    const auto slotIndex = impl_->activeReceiveSlot;
    auto& slot = impl_->receiveSlots[slotIndex];
    const std::size_t copied = std::min(length, slot.received);
    std::memcpy(data, slot.buffer.data() + slot.offset, copied);
    slot.offset += copied;
    slot.received -= copied;
    if (slot.received == 0) {
        const auto posted = impl_->postReceive(slotIndex);
        if (!posted.isOk()) return posted;
        impl_->activeReceiveSlot = kReceiveQueueDepth;
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

RdmaTransportStats RdmaDataSocket::stats() const noexcept {
#if !CPNETFLUX_HAS_RDMA
    return {};
#else
    return impl_ == nullptr ? RdmaTransportStats{} : impl_->statistics;
#endif
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
