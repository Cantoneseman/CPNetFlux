#include "cpnetflux/core/io/file_download_sender.h"
#include "cpnetflux/core/io/unified_file_transfer.h"
namespace cpnetflux::core::io {
common::Status runFramedFileSenderOnListener(const FileDownloadSenderOptions& d,UniqueFd listener,int controlFd) {
    config::FileTransferOptions o;
    o.path=d.path;o.logicalSourcePath=d.sourcePath;o.transferId=d.transferId;o.connections=d.connections;o.chunkSize=d.chunkSize;
    o.bufferSize=d.bufferSize;o.checksumAlgorithm=d.checksumAlgorithm;o.checksumBackend=d.checksumBackend;
    o.fileIo=d.fileIo;o.dataTls=d.dataTls;o.dataTlsMode=d.dataTlsMode;
    o.runtimeMetrics=d.runtimeMetrics;o.hotPathCompression=d.hotPathCompression;o.resume=d.resume;
    return sendUnifiedFile(o,std::move(listener),controlFd);
}
}
