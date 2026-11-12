#include "cpnetflux/core/io/unified_file_transfer.h"
#include "cpnetflux/core/io/persistent_tree_transfer.h"
#include "cpnetflux/checkpoint/transfer_manifest.h"
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <thread>
#include <mutex>
#include <sstream>
#include <atomic>
#include <condition_variable>

namespace cpnetflux::core::io {
namespace {
using common::Status;
namespace fs = std::filesystem;
std::string newId() {
    static std::atomic<unsigned long> serial{0};
    return "cpnetflux-" + std::to_string(::getpid()) + "-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(serial.fetch_add(1));
}
common::Result<std::vector<FramedDataSocket>> sockets(const config::FileTransferOptions& o,
    UniqueFd listener, int controlFd) {
    if (!o.connections || o.connections > kUnifiedFileMaxChannels)
        return Status::invalidArgument("invalid unified data channel count");
    std::vector<FramedDataSocket> data;
    data.reserve(o.connections);
    for (unsigned c=0;c<o.connections;++c) {
        common::Result<FramedDataSocket> connection = Status::runtimeError("unconnected channel");
        if (listener.isValid()) {
            pollfd descriptors[2]{{listener.get(),POLLIN,0},{controlFd,POLLRDHUP,0}};
            const int count = controlFd < 0 ? 1 : 2;
            int ready;
            do { ready=::poll(descriptors,count,30000); } while (ready<0 && errno==EINTR);
            if (ready<=0 || (count==2 && descriptors[1].revents))
                return Status::runtimeError("unified data accept timed out or cancelled");
            if (!(descriptors[0].revents&POLLIN)) return Status::runtimeError("unified listener unavailable");
            connection=acceptFramedDataSocket(UniqueFd(::accept4(listener.get(),nullptr,nullptr,SOCK_CLOEXEC)),
                o.dataTlsMode,o.dataTls);
        } else connection=connectFramedDataSocket(o.host.c_str(),o.port,o.dataTlsMode,o.dataTls);
        if (!connection.isOk()) return connection.status();
        data.push_back(std::move(connection.value()));
    }
    return data;
}
void summary(const config::FileTransferOptions& o, const std::vector<std::unique_ptr<PersistentTreeStats>>& stats,
             const char* label, bool sender, double wall) {
    std::uint64_t bytes=0,wire=0,skipped=0,loaded=0,corrupt=0;
    std::uint64_t payloadPipelineBudgetBytes=0, payloadPipelineHighWatermarkBytes=0;
    std::uint64_t payloadPipelineProducerWaits=0, payloadPipelineConsumerWaits=0;
    TransferRuntimeMetrics runtime;
    storage::FileIoStats io;
    for (const auto& s:stats) {
        bytes+=s->payloadBytes;wire+=s->wireBytes;skipped+=s->skippedBytes;
        io.mergeFrom(s->fileIoStats);loaded+=s->loadedVerifiedChunks;corrupt+=s->removedCorruptChunks;
        payloadPipelineBudgetBytes=std::max(payloadPipelineBudgetBytes,s->payloadPipelineBudgetBytes);
        payloadPipelineHighWatermarkBytes=std::max(payloadPipelineHighWatermarkBytes,s->payloadPipelineHighWatermarkBytes);
        payloadPipelineProducerWaits+=s->payloadPipelineProducerWaits;
        payloadPipelineConsumerWaits+=s->payloadPipelineConsumerWaits;
        runtime.compressionAttempts+=s->runtime.compressionAttempts;
        runtime.compressedFrames+=s->runtime.compressedFrames;
        runtime.rawFallbackFrames+=s->runtime.rawFallbackFrames;
        runtime.compressionFailures+=s->runtime.compressionFailures;
        runtime.decompressionFailures+=s->runtime.decompressionFailures;
        runtime.compressedLogicalBytes+=s->runtime.compressedLogicalBytes;
        runtime.compressedWireBytes+=s->runtime.compressedWireBytes;
        if (!s->runtime.compressionFallbackReason.empty())
            runtime.compressionFallbackReason=s->runtime.compressionFallbackReason;
    }
    runtime.logicalBytes=bytes;runtime.wireBytes=wire;runtime.elapsedSeconds=wall;
    if (o.runtimeMetrics) *o.runtimeMetrics=runtime;
    std::ostringstream output;
    output << label << (sender ? " sent_bytes=" : " received_bytes=") << bytes
        << " actual_mode=v3 wire_bytes=" << wire << " skipped_bytes=" << skipped
        << " removed_corrupt_chunks=" << corrupt << " loaded_verified_chunks=" << loaded
        << " resent_bytes=" << bytes << " verified_bytes=" << bytes+skipped
        << " checksum_backend=" << checksum::checksumBackendName(o.checksumBackend)
        << " compressed_frames=" << runtime.compressedFrames
        << " compression_attempts=" << runtime.compressionAttempts
        << " raw_fallback_frames=" << runtime.rawFallbackFrames
        << " compression_failures=" << runtime.compressionFailures
        << " decompression_failures=" << runtime.decompressionFailures
        << " compressed_logical_bytes=" << runtime.compressedLogicalBytes
        << " compressed_wire_bytes=" << runtime.compressedWireBytes
        << " compression_fallback_reason=" << runtime.compressionFallbackReason
        << " elapsed_seconds=" << wall << " connections=" << o.connections
        << " file_io_backend=" << storage::fileIoBackendName(o.fileIo.backend)
        << " payload_pipeline_budget_bytes=" << payloadPipelineBudgetBytes
        << " payload_pipeline_high_watermark_bytes=" << payloadPipelineHighWatermarkBytes
        << " payload_pipeline_producer_waits=" << payloadPipelineProducerWaits
        << " payload_pipeline_consumer_waits=" << payloadPipelineConsumerWaits;
    storage::appendFileIoStats(output,io);
    std::cout<<output.str()<<std::endl;
}
}
common::Status sendUnifiedFile(const config::FileTransferOptions& o, UniqueFd listener, int controlFd) {
    struct stat st{};
    if (::stat(o.path.c_str(),&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<0)
        return Status::invalidArgument("unified source must be a regular file");
    auto id=o.transferId.empty() ? newId() : o.transferId;
    if (!checkpoint::isValidTransferId(id)) return Status::invalidArgument("invalid transfer identity");
    const auto source=fs::absolute(o.path);
    PersistentFileIdentity base{1,1,static_cast<std::uint64_t>(st.st_size),o.logicalSourcePath.empty() ? source.filename().string() : o.logicalSourcePath,id,
        o.chunkSize,st.st_mtim.tv_sec,o.checksumAlgorithm,{}};
    auto planned=expandPersistentFileRanges({base},o.connections);
    if (!planned.isOk()) return planned.status();
    DynamicFileQueue queue(std::move(planned.value()),o.connections,1,std::min<unsigned>(256,o.connections*2));
    const bool serverSide=listener.isValid();
    auto connected=sockets(o,std::move(listener),controlFd);
    if (!connected.isOk()) return connected.status();
    auto data=std::move(connected.value());
    std::vector<std::unique_ptr<PersistentTreeStats>> observed;
    std::vector<Status> status(o.connections);
    for (unsigned c=0;c<o.connections;++c) observed.push_back(std::make_unique<PersistentTreeStats>());
    std::vector<std::thread> threads;
    std::atomic<bool> stopped{false};
    std::jthread controlMonitor([&](std::stop_token stop) {
        while (!stop.stop_requested() && !stopped.load()) {
            if (controlFd >= 0) {
                pollfd control{controlFd,POLLRDHUP,0};
                if (::poll(&control,1,25)>0 && control.revents) {
                    for (auto& socket:data) (void)::shutdown(socket.fd(),SHUT_RDWR);
                    return;
                }
            } else std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    });
    const auto started=std::chrono::steady_clock::now();
    auto cancel=[&](const Status& failure) {
        queue.cancel(failure);
        for (auto& socket:data) (void)::shutdown(socket.fd(),SHUT_RDWR);
    };
    for (unsigned c=0;c<o.connections;++c) threads.emplace_back([&,c] {
        status[c]=sendPersistentTree(&data[c],source.parent_path().string(),{},observed[c].get(),{},
            1,c,o.connections,&queue,&o);
        if (!status[c].isOk()) cancel(status[c]);
    });
    for (auto& thread:threads) thread.join();
    stopped.store(true);controlMonitor.request_stop();controlMonitor.join();
    summary(o,observed,serverSide ? "file_download_sender" : "file_client",true,std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
    for (const auto& result:status) if (!result.isOk()) return result;
    return Status::ok();
}
common::Status receiveUnifiedFile(const config::FileTransferOptions& o,bool download,
                                  UniqueFd listener,int controlFd) {
    PersistentTransferPolicy policy{o,fs::absolute(o.path).string()};
    auto context=makePersistentTransferContext(policy);
    const bool serverSide=listener.isValid();
    auto connected=sockets(o,std::move(listener),controlFd);
    if (!connected.isOk()) return connected.status();
    auto data=std::move(connected.value());
    std::vector<std::unique_ptr<PersistentTreeStats>> observed;
    std::vector<Status> status(o.connections);
    for (unsigned c=0;c<o.connections;++c) observed.push_back(std::make_unique<PersistentTreeStats>());
    std::mutex mutex;
    std::optional<PersistentFileIdentity> identity;
    auto callback=[&](const PersistentFileIdentity& file,const Status& result,bool complete) {
        if (!result.isOk()) return result;
        std::lock_guard lock(mutex);
        auto base=file;base.rangeId=base.rangeOffset=base.rangeLength=base.rangeCount=base.attempt=0;
        if (identity && !samePersistentIdentity(*identity,base))
            return Status::invalidArgument("singleton received foreign file identity");
        identity=base;
        if (!o.transferId.empty() && file.transferId!=o.transferId)
            return Status::invalidArgument("singleton transfer identity mismatch");
        return Status::ok();
    };
    std::vector<std::thread> threads;
    std::atomic<bool> stopped{false};
    std::jthread controlMonitor([&](std::stop_token stop) {
        while (!stop.stop_requested() && !stopped.load()) {
            if (controlFd >= 0) {
                pollfd control{controlFd,POLLRDHUP,0};
                if (::poll(&control,1,25)>0 && control.revents) {
                    for (auto& socket:data) (void)::shutdown(socket.fd(),SHUT_RDWR);
                    return;
                }
            } else std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    });
    const auto started=std::chrono::steady_clock::now();
    for (unsigned c=0;c<o.connections;++c) threads.emplace_back([&,c] {
        status[c]=receivePersistentTree(&data[c],fs::absolute(o.path).parent_path().string(),download,"",
            observed[c].get(),callback,1,c,o.connections,kUnknownPersistentFileCount,true,o.resume,context);
        if (!status[c].isOk()) for (auto& socket:data) (void)::shutdown(socket.fd(),SHUT_RDWR);
    });
    for (auto& thread:threads) thread.join();
    stopped.store(true);controlMonitor.request_stop();controlMonitor.join();
    summary(o,observed,download ? "file_download_client" : "file_server",false,std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
    for (const auto& result:status) if (!result.isOk()) return result;
    if (!identity) return Status::invalidArgument("singleton did not receive a file");
    return Status::ok();
}
}
