#include "cpnetflux/core/io/rdma_data_socket.h"
#include "cpnetflux/core/protocol/frame.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <ctime>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using cpnetflux::common::Status;
using cpnetflux::core::io::RdmaDataSocket;
using cpnetflux::core::io::RdmaListener;
using cpnetflux::core::io::RdmaTransportStats;
using cpnetflux::core::io::kRdmaDataMessageBufferBytes;
using cpnetflux::core::protocol::EncodedFrameHeader;
using cpnetflux::core::protocol::FrameHeader;
using cpnetflux::core::protocol::FrameType;

constexpr std::uint8_t kPayloadByte = 0x5a;
constexpr std::uint8_t kCompletionByte = 0xa5;
constexpr std::size_t kPayloadBytes = 64U << 10;

bool parseUnsigned(std::string_view text, std::uint64_t* value) {
    if (value == nullptr || text.empty()) return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), *value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

void printResult(const char* role, std::uint64_t logicalBytes, std::uint64_t wireBytes,
                 std::uint64_t frames, double elapsedSeconds,
                 double processCpuSeconds,
                 const RdmaTransportStats& stats) {
    const double throughputGbps = elapsedSeconds > 0.0
        ? static_cast<double>(logicalBytes) * 8.0 / elapsedSeconds / 1.0e9
        : 0.0;
    const double wireThroughputGbps = elapsedSeconds > 0.0
        ? static_cast<double>(wireBytes) * 8.0 / elapsedSeconds / 1.0e9
        : 0.0;
    std::cout << "CPNETFLUX_RDMA_SOCKET_BENCH {\"role\":\"" << role
              << "\",\"result\":\"pass\",\"logical_bytes\":" << logicalBytes
              << ",\"wire_bytes\":" << wireBytes
              << ",\"frame_count\":" << frames
              << ",\"elapsed_seconds\":" << elapsedSeconds
              << ",\"process_cpu_seconds\":" << processCpuSeconds
              << ",\"throughput_gbps\":" << throughputGbps
              << ",\"wire_throughput_gbps\":" << wireThroughputGbps
              << ",\"configured_send_queue_depth\":"
              << cpnetflux::core::io::kRdmaDataQueueDepth
              << ",\"configured_receive_queue_depth\":"
              << cpnetflux::core::io::kRdmaDataReceiveQueueDepth
              << ",\"negotiated_send_queue_depth\":"
              << stats.negotiatedSendQueueDepth
              << ",\"negotiated_receive_queue_depth\":"
              << stats.negotiatedReceiveQueueDepth
              << ",\"negotiated_path_mtu_bytes\":"
              << stats.negotiatedPathMtuBytes
              << ",\"negotiated_min_rnr_timer\":"
              << stats.negotiatedMinRnrTimer
              << ",\"negotiated_rnr_retry\":"
              << stats.negotiatedRnrRetry
              << ",\"slot_bytes\":" << kRdmaDataMessageBufferBytes
              << ",\"send_bytes\":" << stats.sendBytes
              << ",\"send_work_requests\":" << stats.sendWorkRequests
              << ",\"send_completions\":" << stats.sendCompletions
              << ",\"send_completion_poll_calls\":"
              << stats.sendCompletionPollCalls
              << ",\"send_completion_event_waits\":"
              << stats.sendCompletionEventWaits
              << ",\"send_completion_wait_seconds\":"
              << static_cast<double>(stats.sendCompletionWaitNanoseconds) / 1.0e9
              << ",\"max_send_inflight\":" << stats.maxSendInFlight
              << ",\"receive_bytes\":" << stats.receiveBytes
              << ",\"receive_completions\":" << stats.receiveCompletions
              << ",\"receive_post_calls\":" << stats.receivePostCalls
              << ",\"receive_reposts\":" << stats.receiveReposts
              << ",\"receive_completion_poll_calls\":"
              << stats.receiveCompletionPollCalls
              << ",\"receive_ready_queue_high_watermark\":" << stats.receiveReadyQueueHighWatermark
              << ",\"receive_completion_event_waits\":"
              << stats.receiveCompletionEventWaits
              << ",\"receive_completion_wait_seconds\":"
              << static_cast<double>(stats.receiveCompletionWaitNanoseconds) / 1.0e9
              << "}\n";
}

int runServer(const char* host, std::uint16_t port, std::uint64_t bytes) {
    auto listener = RdmaListener::listen(host, port, 1);
    if (!listener.isOk()) {
        std::cerr << "RDMA listen failed: " << listener.status().message() << '\n';
        return 1;
    }
    auto accepted = listener.value().accept();
    if (!accepted.isOk()) {
        std::cerr << "RDMA accept failed: " << accepted.status().message() << '\n';
        return 1;
    }
    auto socket = std::move(accepted.value());
    std::vector<std::uint8_t> payload(kPayloadBytes, kPayloadByte);
    std::uint64_t completed = 0;
    std::uint64_t frames = 0;
    std::uint64_t wireBytes = 0;
    const auto cpuStarted = std::clock();
    const auto started = std::chrono::steady_clock::now();
    while (completed < bytes) {
        const auto length = static_cast<std::size_t>(
            std::min<std::uint64_t>(payload.size(), bytes - completed));
        EncodedFrameHeader encoded{};
        auto status = socket.readAll(encoded.data(), encoded.size());
        if (!status.isOk()) {
            std::cerr << "RDMA frame header read failed: " << status.message() << '\n';
            return 1;
        }
        auto decoded = cpnetflux::core::protocol::decodeFrameHeader(encoded);
        if (!decoded.isOk() || decoded.value().type != FrameType::Data ||
            decoded.value().streamId != 1 || decoded.value().chunkId != 1 ||
            decoded.value().offset != completed || decoded.value().totalSize != bytes ||
            decoded.value().payloadSize != length) {
            std::cerr << "RDMA frame header verification failed at byte " << completed << '\n';
            return 1;
        }
        status = socket.readAll(payload.data(), length);
        if (!status.isOk()) {
            std::cerr << "RDMA frame payload read failed: " << status.message() << '\n';
            return 1;
        }
        if (!std::all_of(payload.begin(), payload.begin() + length,
                         [](std::uint8_t value) { return value == kPayloadByte; })) {
            std::cerr << "RDMA payload verification failed at byte " << completed << '\n';
            return 1;
        }
        completed += length;
        ++frames;
        wireBytes += encoded.size() + length;
    }
    const std::uint8_t completion = kCompletionByte;
    auto status = socket.writeAll(&completion, sizeof(completion));
    if (!status.isOk()) {
        std::cerr << "RDMA completion reply failed: " << status.message() << '\n';
        return 1;
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const double processCpuSeconds = static_cast<double>(std::clock() - cpuStarted) /
                                     static_cast<double>(CLOCKS_PER_SEC);
    printResult("server", completed, wireBytes, frames, elapsed,
                processCpuSeconds, socket.stats());
    return 0;
}

int runClient(const char* host, std::uint16_t port, std::uint64_t bytes,
              std::string_view localBindAddress) {
    auto connected = RdmaDataSocket::connect(host, port, {}, localBindAddress);
    if (!connected.isOk()) {
        std::cerr << "RDMA connect failed: " << connected.status().message() << '\n';
        return 1;
    }
    auto socket = std::move(connected.value());
    std::vector<std::uint8_t> payload(kPayloadBytes, kPayloadByte);
    std::uint64_t completed = 0;
    std::uint64_t frames = 0;
    std::uint64_t wireBytes = 0;
    const auto cpuStarted = std::clock();
    const auto started = std::chrono::steady_clock::now();
    while (completed < bytes) {
        const auto length = static_cast<std::size_t>(
            std::min<std::uint64_t>(payload.size(), bytes - completed));
        FrameHeader header;
        header.type = FrameType::Data;
        header.streamId = 1;
        header.chunkId = 1;
        header.offset = completed;
        header.totalSize = bytes;
        header.payloadSize = static_cast<std::uint32_t>(length);
        const auto encoded = cpnetflux::core::protocol::encodeFrameHeader(header);
        auto status = socket.writeFrameBuffered(encoded.data(), encoded.size(), nullptr, 0,
                                                payload.data(), length);
        if (!status.isOk()) {
            std::cerr << "RDMA frame write failed: " << status.message() << '\n';
            return 1;
        }
        completed += length;
        ++frames;
        wireBytes += encoded.size() + length;
    }
    std::uint8_t completion = 0;
    auto status = socket.readAll(&completion, sizeof(completion));
    if (!status.isOk() || completion != kCompletionByte) {
        std::cerr << "RDMA completion reply was not received or was invalid\n";
        return 1;
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    const double processCpuSeconds = static_cast<double>(std::clock() - cpuStarted) /
                                     static_cast<double>(CLOCKS_PER_SEC);
    printResult("client", completed, wireBytes, frames, elapsed,
                processCpuSeconds, socket.stats());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5 && argc != 6) {
        std::cerr << "Usage: " << argv[0]
                  << " server <bind-ip> <port> <bytes>\n       " << argv[0]
                  << " client <peer-ip> <port> <bytes> <local-bind-ip>\n";
        return 2;
    }
    const std::string_view mode(argv[1]);
    std::uint64_t portValue = 0;
    std::uint64_t bytes = 0;
    if (!parseUnsigned(argv[3], &portValue) || portValue == 0 || portValue > 65535 ||
        !parseUnsigned(argv[4], &bytes) || bytes == 0) {
        std::cerr << "Port and byte count must be positive decimal integers\n";
        return 2;
    }
    const auto port = static_cast<std::uint16_t>(portValue);
    if (mode == "server" && argc == 5) return runServer(argv[2], port, bytes);
    if (mode == "client" && argc == 6)
        return runClient(argv[2], port, bytes, argv[5]);
    std::cerr << "Invalid mode or argument count\n";
    return 2;
}
