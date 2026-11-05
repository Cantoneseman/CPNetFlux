#include "cpnetflux/core/io/rdma_data_socket.h"
#include "cpnetflux/core/io/detail/rdma_completion_wait.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
constexpr std::size_t kReceiveSpareDepth = kRdmaDataReceiveSpareDepth;
constexpr std::size_t kReceiveSlotCount = kReceiveQueueDepth + kReceiveSpareDepth;
constexpr std::size_t kCompletionQueueDepth = kReceiveQueueDepth * 2;
constexpr int kDefaultTimeoutMs = 30000;
std::atomic<std::uint64_t> gNextRdmaConnectionId{1};

int timeoutMs(SocketDeadline deadline) {
    if (!deadline) return kDefaultTimeoutMs;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        *deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) return 1;
    return static_cast<int>(std::min<std::int64_t>(remaining.count(), kDefaultTimeoutMs));
}

common::Status rdmaStatus(const char* operation, int errorNumber = errno) {
    return common::Status::transportError(
        std::string(operation) + ": " + std::strerror(errorNumber), errorNumber);
}

common::Status waitForCompletionEvent(ibv_comp_channel* channel, ibv_cq* expectedCq,
                                      SocketDeadline deadline,
                                      std::uint64_t* eventWaits,
                                      const std::atomic_bool* disconnected) {
    if (channel == nullptr || expectedCq == nullptr || eventWaits == nullptr)
        return common::Status::invalidArgument("invalid RDMA completion event wait");

    pollfd descriptor{channel->fd, POLLIN, 0};
    while (true) {
        if (disconnected->load(std::memory_order_acquire))
            return common::Status::transportError("RDMA socket shut down");
        ++*eventWaits;
        const auto now = std::chrono::steady_clock::now();
        if (detail::rdmaCompletionDeadlineExpired(deadline, now))
            return common::Status::transportError("RDMA completion event timed out");
        const int ready = ::poll(&descriptor, 1,
            detail::rdmaCompletionEventPollTimeoutMs(deadline, now));
        if (ready < 0 && errno == EINTR) continue;
        if (ready == 0) {
            if (detail::rdmaCompletionDeadlineExpired(deadline,
                    std::chrono::steady_clock::now()))
                return common::Status::transportError("RDMA completion event timed out");
            continue;
        }
        if (ready < 0) return rdmaStatus("poll RDMA completion event");
        if (!(descriptor.revents & POLLIN))
            return common::Status::transportError("RDMA completion event channel closed");

        ibv_cq* eventCq = nullptr;
        void* cqContext = nullptr;
        if (::ibv_get_cq_event(channel, &eventCq, &cqContext) != 0)
            return rdmaStatus("ibv_get_cq_event");
        (void)cqContext;
        ::ibv_ack_cq_events(eventCq, 1);
        if (eventCq != expectedCq)
            return common::Status::transportError("RDMA completion arrived on an unexpected CQ");
        return common::Status::ok();
    }
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
        std::uint8_t* buffer = nullptr;
        bool inFlight = false;
    };

    struct ReceiveSlot {
        std::uint8_t* buffer = nullptr;
        std::size_t received = 0;
        std::size_t offset = 0;
        bool posted = false;
        std::chrono::steady_clock::time_point completionObservedAt{};
    };

    rdma_event_channel* channel = nullptr;
    rdma_cm_id* id = nullptr;
    ibv_pd* pd = nullptr;
    ibv_comp_channel* sendCompletionChannel = nullptr;
    ibv_comp_channel* receiveCompletionChannel = nullptr;
    ibv_cq* sendCq = nullptr;
    ibv_cq* recvCq = nullptr;
    std::vector<std::uint8_t> sendBufferPool;
    std::vector<std::uint8_t> receiveBufferPool;
    ibv_mr* sendMemoryRegion = nullptr;
    ibv_mr* receiveMemoryRegion = nullptr;
    std::array<SendSlot, kSendQueueDepth> sendSlots;
    std::array<ReceiveSlot, kReceiveSlotCount> receiveSlots;
    std::array<std::size_t, kReceiveSpareDepth> freeReceiveSlotQueue{};
    std::size_t freeReceiveSlotHead = 0;
    std::size_t freeReceiveSlotTail = 0;
    std::size_t freeReceiveSlotCount = 0;
    std::array<std::size_t, kReceiveSpareDepth> readyReceiveSlotQueue{};
    std::size_t readyReceiveSlotHead = 0;
    std::size_t readyReceiveSlotTail = 0;
    std::size_t readyReceiveSlotCount = 0;
    std::size_t sendInFlight = 0;
    std::size_t stagedSendSlot = kSendQueueDepth;
    std::size_t stagedSendBytes = 0;
    std::size_t activeReceiveSlot = kReceiveSlotCount;
    RdmaTransportStats statistics;
    bool closed = false;
    std::atomic_bool disconnected{false};
    const bool diagnostics = std::getenv("CPNETFLUX_RDMA_DIAGNOSTICS") != nullptr;
    const std::uint64_t connectionId = diagnostics
        ? gNextRdmaConnectionId.fetch_add(1) : 0;
    const char* role = "unknown";
    std::uint32_t queuePairNumber = 0;
    std::chrono::steady_clock::time_point lastRepost{};

    void trace(const char* event, std::uint64_t duration = 0) const noexcept {
        if (!diagnostics) return;
        const auto now = std::chrono::steady_clock::now();
        const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
        // A writer diagnostic must not read the concurrently owned receive state.
        if (std::strcmp(event, "send_wait_end") == 0) {
            std::fprintf(stderr, "CPNETFLUX_RDMA_TRACE {\"event\":\"%s\",\"monotonic_ns\":%lld,"
                "\"duration_ns\":%llu,\"connection_id\":%llu,\"role\":\"%s\","
                "\"qp_num\":%u,\"send_inflight\":%zu}\n", event,
                static_cast<long long>(stamp), static_cast<unsigned long long>(duration),
                static_cast<unsigned long long>(connectionId), role, queuePairNumber,
                sendInFlight);
            return;
        }
        const auto age = lastRepost == std::chrono::steady_clock::time_point{}
            ? 0 : std::chrono::duration_cast<std::chrono::nanoseconds>(now - lastRepost).count();
        std::fprintf(stderr, "CPNETFLUX_RDMA_TRACE {\"event\":\"%s\",\"monotonic_ns\":%lld,"
            "\"duration_ns\":%llu,\"connection_id\":%llu,\"role\":\"%s\","
            "\"qp_num\":%u,\"received_bytes\":%llu,\"bytes_consumed\":%llu,"
            "\"receive_completions\":%llu,\"reposts\":%llu,"
            "\"observed_posted_receive_depth\":%llu,"
            "\"minimum_observed_posted_receive_depth\":%llu,"
            "\"ready\":%zu,\"ready_high_watermark\":%llu,"
            "\"free\":%zu,\"free_low_watermark\":%llu,"
            "\"completion_to_repost_total_ns\":%llu,"
            "\"completion_to_repost_max_ns\":%llu,"
            "\"completion_to_repost_over_640us\":%llu,"
            "\"completion_to_consumer_total_ns\":%llu,"
            "\"completion_to_consumer_max_ns\":%llu,"
            "\"spare_exhausted\":%llu,\"drain_blocked_no_spare\":%llu,"
            "\"last_repost_age_ns\":%lld}\n", event, static_cast<long long>(stamp),
            static_cast<unsigned long long>(duration),
            static_cast<unsigned long long>(connectionId), role, queuePairNumber,
            static_cast<unsigned long long>(statistics.receiveBytes),
            static_cast<unsigned long long>(statistics.receiveBytesConsumed),
            static_cast<unsigned long long>(statistics.receiveCompletions),
            static_cast<unsigned long long>(statistics.receiveReposts),
            static_cast<unsigned long long>(statistics.observedPostedReceiveDepth),
            static_cast<unsigned long long>(statistics.minimumObservedPostedReceiveDepth),
            readyReceiveSlotCount,
            static_cast<unsigned long long>(statistics.receiveReadyQueueHighWatermark),
            freeReceiveSlotCount,
            static_cast<unsigned long long>(statistics.receiveFreeQueueLowWatermark),
            static_cast<unsigned long long>(statistics.receiveCompletionToRepostNanoseconds),
            static_cast<unsigned long long>(statistics.receiveCompletionToRepostMaxNanoseconds),
            static_cast<unsigned long long>(statistics.receiveCompletionToRepostOver640Microseconds),
            static_cast<unsigned long long>(statistics.receiveCompletionToConsumerNanoseconds),
            static_cast<unsigned long long>(statistics.receiveCompletionToConsumerMaxNanoseconds),
            static_cast<unsigned long long>(statistics.receiveSpareExhaustedCount),
            static_cast<unsigned long long>(statistics.receiveDrainBlockedNoSpareCount),
            static_cast<long long>(age));
    }

    ~Impl() { close(); }

    void close() noexcept {
        if (closed) return;
        closed = true;
        const auto closeStarted = std::chrono::steady_clock::now();
        trace("close_begin");
        if (id != nullptr && !disconnected.exchange(true)) {
            const auto disconnectStarted = std::chrono::steady_clock::now();
            (void)::rdma_disconnect(id);
            trace("disconnect_end", elapsedNanoseconds(disconnectStarted));
        } else {
            trace("disconnect_end");
        }
        if (id != nullptr && id->qp != nullptr) ::rdma_destroy_qp(id);
        trace("qp_destroyed", elapsedNanoseconds(closeStarted));
        if (sendMemoryRegion != nullptr) (void)::ibv_dereg_mr(sendMemoryRegion);
        if (receiveMemoryRegion != nullptr) (void)::ibv_dereg_mr(receiveMemoryRegion);
        sendMemoryRegion = nullptr;
        receiveMemoryRegion = nullptr;
        if (sendCq != nullptr) (void)::ibv_destroy_cq(sendCq);
        if (recvCq != nullptr) (void)::ibv_destroy_cq(recvCq);
        if (sendCompletionChannel != nullptr)
            (void)::ibv_destroy_comp_channel(sendCompletionChannel);
        if (receiveCompletionChannel != nullptr)
            (void)::ibv_destroy_comp_channel(receiveCompletionChannel);
        if (pd != nullptr) (void)::ibv_dealloc_pd(pd);
        if (id != nullptr) (void)::rdma_destroy_id(id);
        if (channel != nullptr) ::rdma_destroy_event_channel(channel);
        channel = nullptr;
        id = nullptr;
        pd = nullptr;
        sendCompletionChannel = nullptr;
        receiveCompletionChannel = nullptr;
        sendCq = nullptr;
        recvCq = nullptr;
        trace("close_end", elapsedNanoseconds(closeStarted));
    }

    common::Status initializeQueuePair() {
        const auto initializeStarted = std::chrono::steady_clock::now();
        if (id == nullptr || id->verbs == nullptr) return common::Status::runtimeError(
            "RDMA connection has no verbs context");
        pd = ::ibv_alloc_pd(id->verbs);
        if (pd == nullptr) return rdmaStatus("ibv_alloc_pd");
        sendCompletionChannel = ::ibv_create_comp_channel(id->verbs);
        receiveCompletionChannel = ::ibv_create_comp_channel(id->verbs);
        if (sendCompletionChannel == nullptr || receiveCompletionChannel == nullptr)
            return rdmaStatus("ibv_create_comp_channel");
        sendCq = ::ibv_create_cq(id->verbs, kCompletionQueueDepth, nullptr,
                                 sendCompletionChannel, 0);
        recvCq = ::ibv_create_cq(id->verbs, kCompletionQueueDepth, nullptr,
                                 receiveCompletionChannel, 0);
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
        queuePairNumber = id->qp->qp_num;
        if (diagnostics) {
            statistics.connectionId = connectionId;
            statistics.queuePairNumber = queuePairNumber;
        }
        auto started = std::chrono::steady_clock::now();
        sendBufferPool.resize(sendSlots.size() * kMessageBufferSize);
        statistics.bufferAllocateNanoseconds += elapsedNanoseconds(started);
        started = std::chrono::steady_clock::now();
        sendMemoryRegion = ::ibv_reg_mr(pd, sendBufferPool.data(), sendBufferPool.size(), 0);
        statistics.memoryRegisterNanoseconds += elapsedNanoseconds(started);
        if (sendMemoryRegion == nullptr) return rdmaStatus("ibv_reg_mr send buffer pool");
        for (std::size_t index = 0; index < sendSlots.size(); ++index) {
            sendSlots[index].buffer = sendBufferPool.data() + index * kMessageBufferSize;
        }
        started = std::chrono::steady_clock::now();
        receiveBufferPool.resize(receiveSlots.size() * kMessageBufferSize);
        statistics.bufferAllocateNanoseconds += elapsedNanoseconds(started);
        started = std::chrono::steady_clock::now();
        receiveMemoryRegion = ::ibv_reg_mr(pd, receiveBufferPool.data(),
                                           receiveBufferPool.size(), IBV_ACCESS_LOCAL_WRITE);
        statistics.memoryRegisterNanoseconds += elapsedNanoseconds(started);
        if (receiveMemoryRegion == nullptr) return rdmaStatus("ibv_reg_mr receive buffer pool");
        for (std::size_t index = 0; index < receiveSlots.size(); ++index) {
            auto& slot = receiveSlots[index];
            slot.buffer = receiveBufferPool.data() + index * kMessageBufferSize;
        }
        started = std::chrono::steady_clock::now();
        for (std::size_t index = 0; index < kReceiveQueueDepth; ++index) {
            const auto status = postReceive(index);
            if (!status.isOk()) return status;
        }
        for (std::size_t index = kReceiveQueueDepth; index < kReceiveSlotCount; ++index) {
            if (!enqueueFreeReceiveSlot(index))
                return common::Status::runtimeError("RDMA receive spare queue is full");
        }
        if (diagnostics) {
            statistics.minimumObservedPostedReceiveDepth =
                statistics.observedPostedReceiveDepth;
            statistics.receiveFreeQueueLowWatermark = freeReceiveSlotCount;
        }
        statistics.receivePrepostNanoseconds = elapsedNanoseconds(started);
        statistics.queuePairInitializeNanoseconds = elapsedNanoseconds(initializeStarted);
        trace("queue_pair_initialized", statistics.queuePairInitializeNanoseconds);
        return common::Status::ok();
    }

    static std::uint64_t elapsedNanoseconds(std::chrono::steady_clock::time_point start) {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count());
    }

    bool enqueueFreeReceiveSlot(std::size_t index) {
        if (freeReceiveSlotCount >= freeReceiveSlotQueue.size()) return false;
        freeReceiveSlotQueue[freeReceiveSlotTail] = index;
        freeReceiveSlotTail = (freeReceiveSlotTail + 1) % freeReceiveSlotQueue.size();
        ++freeReceiveSlotCount;
        if (diagnostics)
            statistics.receiveFreeQueueLowWatermark = std::min<std::uint64_t>(
                statistics.receiveFreeQueueLowWatermark, freeReceiveSlotCount);
        return true;
    }

    bool dequeueFreeReceiveSlot(std::size_t* index) {
        if (index == nullptr || freeReceiveSlotCount == 0) return false;
        *index = freeReceiveSlotQueue[freeReceiveSlotHead];
        freeReceiveSlotHead = (freeReceiveSlotHead + 1) % freeReceiveSlotQueue.size();
        --freeReceiveSlotCount;
        if (diagnostics)
            statistics.receiveFreeQueueLowWatermark = std::min<std::uint64_t>(
                statistics.receiveFreeQueueLowWatermark, freeReceiveSlotCount);
        return true;
    }

    bool enqueueReadyReceiveSlot(std::size_t index) {
        if (readyReceiveSlotCount >= readyReceiveSlotQueue.size()) return false;
        readyReceiveSlotQueue[readyReceiveSlotTail] = index;
        readyReceiveSlotTail = (readyReceiveSlotTail + 1) % readyReceiveSlotQueue.size();
        ++readyReceiveSlotCount;
        statistics.receiveReadyQueueHighWatermark = std::max<std::uint64_t>(
            statistics.receiveReadyQueueHighWatermark, readyReceiveSlotCount);
        return true;
    }

    bool dequeueReadyReceiveSlot(std::size_t* index) {
        if (index == nullptr || readyReceiveSlotCount == 0) return false;
        *index = readyReceiveSlotQueue[readyReceiveSlotHead];
        readyReceiveSlotHead = (readyReceiveSlotHead + 1) % readyReceiveSlotQueue.size();
        --readyReceiveSlotCount;
        return true;
    }

    common::Status configureReceiveRetryTimer() {
        // CM defaults to encoding 0 (655 ms). A short RQ shortage must not
        // leave a replenished receiver idle for that entire retry interval.
        ibv_qp_attr attributes{};
        attributes.min_rnr_timer = 12;  // IBTA encoding: 0.64 ms.
        const int result = ::ibv_modify_qp(id->qp, &attributes, IBV_QP_MIN_RNR_TIMER);
        if (result != 0)
            return rdmaStatus("ibv_modify_qp minimum RNR timer", result > 0 ? result : errno);
        return common::Status::ok();
    }

    void captureQueuePairObservations() {
        ibv_qp_attr attributes{};
        ibv_qp_init_attr initialAttributes{};
        if (::ibv_query_qp(id->qp, &attributes,
                           IBV_QP_STATE | IBV_QP_PATH_MTU |
                               IBV_QP_MIN_RNR_TIMER | IBV_QP_RNR_RETRY,
                           &initialAttributes) != 0)
            return;

        statistics.negotiatedSendQueueDepth = initialAttributes.cap.max_send_wr;
        statistics.negotiatedReceiveQueueDepth = initialAttributes.cap.max_recv_wr;
        statistics.negotiatedMinRnrTimer = attributes.min_rnr_timer;
        statistics.negotiatedRnrRetry = attributes.rnr_retry;
        switch (attributes.path_mtu) {
            case IBV_MTU_256: statistics.negotiatedPathMtuBytes = 256; break;
            case IBV_MTU_512: statistics.negotiatedPathMtuBytes = 512; break;
            case IBV_MTU_1024: statistics.negotiatedPathMtuBytes = 1024; break;
            case IBV_MTU_2048: statistics.negotiatedPathMtuBytes = 2048; break;
            case IBV_MTU_4096: statistics.negotiatedPathMtuBytes = 4096; break;
            default: statistics.negotiatedPathMtuBytes = 0; break;
        }
    }

    common::Status postReceive(std::size_t index) {
        if (index >= receiveSlots.size())
            return common::Status::invalidArgument("invalid RDMA receive slot");
        auto& slot = receiveSlots[index];
        if (receiveMemoryRegion == nullptr || slot.buffer == nullptr || slot.posted)
            return common::Status::runtimeError("RDMA receive slot is not reusable");
        ibv_sge scatter{reinterpret_cast<std::uintptr_t>(slot.buffer),
                        static_cast<std::uint32_t>(kMessageBufferSize), receiveMemoryRegion->lkey};
        ibv_recv_wr workRequest{};
        workRequest.wr_id = index;
        workRequest.sg_list = &scatter;
        workRequest.num_sge = 1;
        ibv_recv_wr* bad = nullptr;
        if (::ibv_post_recv(id->qp, &workRequest, &bad) != 0) return rdmaStatus("ibv_post_recv");
        slot.posted = true;
        ++statistics.receivePostCalls;
        if (diagnostics) ++statistics.observedPostedReceiveDepth;
        if (diagnostics && statistics.receivePostCalls > kReceiveQueueDepth) {
            const auto now = std::chrono::steady_clock::now();
            if (lastRepost != std::chrono::steady_clock::time_point{}) {
                const auto gap = elapsedNanoseconds(lastRepost);
                if (gap >= 1000000) trace("repost_gap", gap);
            }
            lastRepost = now;
        }
        return common::Status::ok();
    }

    void recordReceiveCompletion(const ibv_wc& completion) {
        ++statistics.receiveCompletions;
        statistics.receiveBytes += completion.byte_len;
        if (diagnostics) {
            if (completion.wr_id < receiveSlots.size())
                receiveSlots[static_cast<std::size_t>(completion.wr_id)].completionObservedAt =
                    std::chrono::steady_clock::now();
            if (statistics.observedPostedReceiveDepth != 0)
                --statistics.observedPostedReceiveDepth;
            statistics.minimumObservedPostedReceiveDepth = std::min<std::uint64_t>(
                statistics.minimumObservedPostedReceiveDepth,
                statistics.observedPostedReceiveDepth);
        }
    }

    common::Status pollReceiveCompletion(ibv_wc* completion) {
        const SocketDeadline deadline{};
        const auto waitStarted = std::chrono::steady_clock::now();
        ++statistics.receiveCompletionWaitCount;
        auto finishWait = [&](common::Status status) {
            const auto duration = elapsedNanoseconds(waitStarted);
            statistics.receiveCompletionWaitNanoseconds += duration;
            statistics.receiveCompletionMaxWaitNanoseconds = std::max(
                statistics.receiveCompletionMaxWaitNanoseconds, duration);
            if (duration >= 1000000) trace("receive_wait_end", duration);
            return status;
        };
        auto pollOnce = [&]() -> common::Result<bool> {
            if (disconnected.load(std::memory_order_acquire))
                return common::Status::transportError("RDMA socket shut down");
            ++statistics.receiveCompletionPollCalls;
            const int count = ::ibv_poll_cq(recvCq, 1, completion);
            if (count < 0)
                return common::Status::transportError("ibv_poll_cq failed");
            if (count == 0) {
                ++statistics.receiveCompletionEmptyPollCalls;
                return false;
            }
            if (count == 1) {
                if (completion->status != IBV_WC_SUCCESS)
                    return common::Status::transportError(
                        "RDMA work completion failed: " +
                        std::string(::ibv_wc_status_str(completion->status)), completion->status);
                recordReceiveCompletion(*completion);
                return true;
            }
            return common::Status::transportError("ibv_poll_cq returned an invalid count");
        };
        for (;;) {
            for (std::uint32_t spin = 0; spin < kRdmaCompletionSpinPollBudget; ++spin) {
                auto polled = pollOnce();
                if (!polled.isOk()) return finishWait(polled.status());
                if (polled.value()) return finishWait(common::Status::ok());
                if (detail::rdmaCompletionDeadlineExpired(
                        deadline, std::chrono::steady_clock::now()))
                    return finishWait(common::Status::transportError(
                        "RDMA work completion timed out"));
            }

            if (::ibv_req_notify_cq(recvCq, 0) != 0)
                return finishWait(rdmaStatus("ibv_req_notify_cq receive"));

            // Recheck after arming so a completion cannot land in the arm/wait gap.
            auto polled = pollOnce();
            if (!polled.isOk()) return finishWait(polled.status());
            if (polled.value()) return finishWait(common::Status::ok());
            if (detail::rdmaCompletionDeadlineExpired(
                    deadline, std::chrono::steady_clock::now())) {
                return finishWait(common::Status::transportError(
                    "RDMA work completion timed out"));
            }

            const auto eventStatus = waitForCompletionEvent(
                receiveCompletionChannel, recvCq, deadline,
                &statistics.receiveCompletionEventWaits, &disconnected);
            if (!eventStatus.isOk()) return finishWait(eventStatus);
        }
    }

    common::Status stageReceiveCompletion(const ibv_wc& completion) {
        if (completion.wr_id >= receiveSlots.size())
            return common::Status::transportError("RDMA completion has an invalid receive slot");
        std::size_t replacement = 0;
        if (!dequeueFreeReceiveSlot(&replacement)) {
            if (diagnostics) ++statistics.receiveSpareExhaustedCount;
            return common::Status::transportError("RDMA receive spare queue is exhausted");
        }
        auto& completed = receiveSlots[static_cast<std::size_t>(completion.wr_id)];
        if (!completed.posted)
            return common::Status::transportError("RDMA completion used an unposted receive slot");
        completed.posted = false;
        completed.received = completion.byte_len;
        completed.offset = 0;
        const auto repostStarted = completed.completionObservedAt;
        const auto posted = postReceive(replacement);
        if (!posted.isOk()) {
            (void)enqueueFreeReceiveSlot(replacement);
            return posted;
        }
        ++statistics.receiveReposts;
        if (diagnostics) {
            const auto repostNanoseconds = elapsedNanoseconds(repostStarted);
            ++statistics.receiveCompletionToRepostCount;
            statistics.receiveCompletionToRepostNanoseconds += repostNanoseconds;
            statistics.receiveCompletionToRepostMaxNanoseconds = std::max(
                statistics.receiveCompletionToRepostMaxNanoseconds, repostNanoseconds);
            if (repostNanoseconds > 640000)
                ++statistics.receiveCompletionToRepostOver640Microseconds;
            if (repostNanoseconds >= 1000000)
                trace("receive_repost_slow", repostNanoseconds);
        }
        if (!enqueueReadyReceiveSlot(static_cast<std::size_t>(completion.wr_id)))
            return common::Status::runtimeError("RDMA ready receive queue is full");
        return common::Status::ok();
    }

    common::Result<std::size_t> drainReceiveCompletions() {
        if (freeReceiveSlotCount == 0) {
            if (diagnostics) ++statistics.receiveDrainBlockedNoSpareCount;
            return std::size_t{0};
        }
        std::array<ibv_wc, kReceiveSpareDepth> completions{};
        const int limit = static_cast<int>(std::min<std::size_t>(
            freeReceiveSlotCount, completions.size()));
        ++statistics.receiveCompletionPollCalls;
        const int count = ::ibv_poll_cq(recvCq, limit, completions.data());
        if (count < 0) return common::Status::transportError("ibv_poll_cq receive drain failed");
        if (count == 0) {
            ++statistics.receiveCompletionEmptyPollCalls;
            return std::size_t{0};
        }
        for (int index = 0; index < count; ++index) {
            const auto& completion = completions[static_cast<std::size_t>(index)];
            if (completion.status != IBV_WC_SUCCESS)
                return common::Status::transportError(
                    "RDMA work completion failed: " +
                    std::string(::ibv_wc_status_str(completion.status)), completion.status);
            recordReceiveCompletion(completion);
            const auto status = stageReceiveCompletion(completion);
            if (!status.isOk()) return status;
        }
        return static_cast<std::size_t>(count);
    }

    common::Status stageNextReceiveCompletion() {
        ibv_wc completion{};
        const auto status = pollReceiveCompletion(&completion);
        if (!status.isOk()) return status;
        return stageReceiveCompletion(completion);
    }

    common::Status releaseActiveReceiveSlot() {
        if (activeReceiveSlot == kReceiveSlotCount)
            return common::Status::runtimeError("RDMA active receive slot is missing");
        auto& slot = receiveSlots[activeReceiveSlot];
        if (diagnostics) {
            const auto holdNanoseconds = elapsedNanoseconds(slot.completionObservedAt);
            ++statistics.receiveSlotReleases;
            ++statistics.receiveSlotHoldCount;
            statistics.receiveSlotHoldNanoseconds += holdNanoseconds;
            statistics.receiveSlotHoldMaxNanoseconds = std::max(
                statistics.receiveSlotHoldMaxNanoseconds, holdNanoseconds);
        }
        slot.received = 0;
        slot.offset = 0;
        if (diagnostics) slot.completionObservedAt = {};
        if (!enqueueFreeReceiveSlot(activeReceiveSlot))
            return common::Status::runtimeError("RDMA receive free queue is full");
        activeReceiveSlot = kReceiveSlotCount;
        return common::Status::ok();
    }

    common::Status activateNextReceiveSlot() {
        if (readyReceiveSlotCount == 0) {
            const auto status = stageNextReceiveCompletion();
            if (!status.isOk()) return status;
        }
        const auto drained = drainReceiveCompletions();
        if (!drained.isOk()) return drained.status();
        if (!dequeueReadyReceiveSlot(&activeReceiveSlot))
            return common::Status::runtimeError("RDMA ready receive queue is empty");
        if (diagnostics) {
            const auto& slot = receiveSlots[activeReceiveSlot];
            const auto consumerNanoseconds = elapsedNanoseconds(slot.completionObservedAt);
            ++statistics.receiveCompletionToConsumerCount;
            statistics.receiveCompletionToConsumerNanoseconds += consumerNanoseconds;
            statistics.receiveCompletionToConsumerMaxNanoseconds = std::max(
                statistics.receiveCompletionToConsumerMaxNanoseconds, consumerNanoseconds);
            if (consumerNanoseconds >= 1000000)
                trace("receive_consumer_wait", consumerNanoseconds);
        }
        return common::Status::ok();
    }

    common::Result<std::size_t> reapSendCompletions() {
        std::array<ibv_wc, kSendQueueDepth> completions{};
        ++statistics.sendCompletionPollCalls;
        const int count = ::ibv_poll_cq(sendCq, static_cast<int>(completions.size()),
                                        completions.data());
        if (count < 0) return common::Status::transportError("ibv_poll_cq send failed");
        if (count == 0) {
            ++statistics.sendCompletionEmptyPollCalls;
            return std::size_t{0};
        }
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
        const SocketDeadline deadline{};
        ++statistics.sendCompletionWaitCount;
        auto finishWait = [&](common::Status status) {
            const auto duration = elapsedNanoseconds(waitStarted);
            statistics.sendCompletionWaitNanoseconds += duration;
            statistics.sendCompletionMaxWaitNanoseconds = std::max(
                statistics.sendCompletionMaxWaitNanoseconds, duration);
            if (duration >= 1000000) trace("send_wait_end", duration);
            return status;
        };
        for (;;) {
            if (disconnected.load(std::memory_order_acquire))
                return finishWait(common::Status::transportError("RDMA socket shut down"));
            for (std::uint32_t spin = 0; spin < kRdmaCompletionSpinPollBudget; ++spin) {
                auto completions = reapSendCompletions();
                if (!completions.isOk()) return finishWait(completions.status());
                if (completions.value() != 0) return finishWait(common::Status::ok());
                if (detail::rdmaCompletionDeadlineExpired(
                        deadline, std::chrono::steady_clock::now()))
                    return finishWait(common::Status::transportError(
                        "RDMA send completion timed out"));
            }

            if (::ibv_req_notify_cq(sendCq, 0) != 0)
                return finishWait(rdmaStatus("ibv_req_notify_cq send"));

            // Recheck after arming so a completion cannot land in the arm/wait gap.
            auto completions = reapSendCompletions();
            if (!completions.isOk()) return finishWait(completions.status());
            if (completions.value() != 0) return finishWait(common::Status::ok());
            if (detail::rdmaCompletionDeadlineExpired(
                    deadline, std::chrono::steady_clock::now()))
                return finishWait(common::Status::transportError(
                    "RDMA send completion timed out"));

            const auto eventStatus = waitForCompletionEvent(
                sendCompletionChannel, sendCq, deadline,
                &statistics.sendCompletionEventWaits, &disconnected);
            if (!eventStatus.isOk()) return finishWait(eventStatus);
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
        ibv_sge scatter{reinterpret_cast<std::uintptr_t>(slot.buffer),
                        static_cast<std::uint32_t>(length), sendMemoryRegion->lkey};
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

    common::Status flushStagedSend() {
        if (stagedSendSlot == kSendQueueDepth) return common::Status::ok();
        const auto status = postSend(stagedSendSlot, stagedSendBytes);
        if (!status.isOk()) return status;
        stagedSendSlot = kSendQueueDepth;
        stagedSendBytes = 0;
        return common::Status::ok();
    }

    common::Status writeSpans(const std::array<std::span<const std::uint8_t>, 3>& spans) {
        const auto stagedStatus = flushStagedSend();
        if (!stagedStatus.isOk()) return stagedStatus;
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
            const std::size_t length = std::min(remaining, kMessageBufferSize);
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
                std::memcpy(slot.buffer + copied,
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

    common::Status writeSpansBuffered(
        const std::array<std::span<const std::uint8_t>, 3>& spans) {
        std::size_t remaining = 0;
        for (const auto& span : spans) {
            if (span.size() > std::numeric_limits<std::size_t>::max() - remaining)
                return common::Status::invalidArgument("RDMA buffered write length overflow");
            remaining += span.size();
        }

        std::size_t spanIndex = 0;
        std::size_t spanOffset = 0;
        while (remaining != 0) {
            if (stagedSendSlot == kSendQueueDepth) {
                const auto available = waitForSendSlot(&stagedSendSlot);
                if (!available.isOk()) return available;
                stagedSendBytes = 0;
            }
            const auto availableBytes = kMessageBufferSize - stagedSendBytes;
            const auto length = std::min(remaining, availableBytes);
            auto* destination = sendSlots[stagedSendSlot].buffer + stagedSendBytes;
            std::size_t copied = 0;
            while (copied < length) {
                while (spanIndex < spans.size() && spanOffset == spans[spanIndex].size()) {
                    ++spanIndex;
                    spanOffset = 0;
                }
                if (spanIndex >= spans.size())
                    return common::Status::runtimeError("RDMA buffered span accounting underflow");
                const auto count = std::min(length - copied,
                                            spans[spanIndex].size() - spanOffset);
                std::memcpy(destination + copied, spans[spanIndex].data() + spanOffset, count);
                copied += count;
                spanOffset += count;
            }
            stagedSendBytes += length;
            remaining -= length;
            if (stagedSendBytes == kMessageBufferSize) {
                const auto status = flushStagedSend();
                if (!status.isOk()) return status;
            }
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
    impl->role = "active";
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
    status = impl->configureReceiveRetryTimer();
    if (!status.isOk()) return status;
    impl->captureQueuePairObservations();
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

common::Status RdmaDataSocket::writeFrameBuffered(const std::uint8_t* header,
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
        return common::Status::invalidArgument("invalid buffered RDMA frame spans");
    return impl_->writeSpansBuffered({std::span<const std::uint8_t>(header, headerLength),
                                      std::span<const std::uint8_t>(prefix, prefixLength),
                                      std::span<const std::uint8_t>(payload, payloadLength)});
#endif
}

common::Status RdmaDataSocket::flushWrites() {
#if !CPNETFLUX_HAS_RDMA
    return unavailable();
#else
    if (!valid()) return common::Status::invalidArgument("invalid RDMA flush");
    return impl_->flushStagedSend();
#endif
}

common::Result<std::size_t> RdmaDataSocket::readSome(std::uint8_t* data, std::size_t length) {
#if !CPNETFLUX_HAS_RDMA
    (void)data; (void)length; return unavailable();
#else
    if (!valid() || data == nullptr || length == 0) return common::Status::invalidArgument("invalid RDMA read");
    // The result reader may overlap the next file's writer. Never touch its SQ here.
    if (impl_->activeReceiveSlot == kReceiveSlotCount) {
        const auto status = impl_->activateNextReceiveSlot();
        if (!status.isOk()) return status;
    }
    const auto slotIndex = impl_->activeReceiveSlot;
    auto& slot = impl_->receiveSlots[slotIndex];
    const std::size_t copied = std::min(length, slot.received);
    if (impl_->diagnostics) impl_->statistics.receiveBytesConsumed += copied;
    std::memcpy(data, slot.buffer + slot.offset, copied);
    slot.offset += copied;
    slot.received -= copied;
    if (slot.received == 0) {
        const auto released = impl_->releaseActiveReceiveSlot();
        if (!released.isOk()) return released;
    }
    const auto drained = impl_->drainReceiveCompletions();
    if (!drained.isOk()) return drained.status();
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
    connection->role = "passive";
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
    status = connection->configureReceiveRetryTimer();
    if (!status.isOk()) return status;
    connection->captureQueuePairObservations();
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
