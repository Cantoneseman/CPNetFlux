#include "cpnetflux/core/io/file_download_client.h"
#include "cpnetflux/core/io/unified_file_transfer.h"
namespace cpnetflux::core::io {
common::Status runFileDownloadClient(const config::FileDownloadOptions& d) {
    config::FileTransferOptions o;
    o.host=d.host;o.port=d.port;o.connections=d.connections;o.bufferSize=d.bufferSize;
    o.path=d.path;o.transferId=d.transferId;o.checksumAlgorithm=d.checksumAlgorithm;
    o.checksumBackend=d.checksumBackend;o.manifestFlushPolicy=d.manifestFlushPolicy;
    o.manifestFlushIntervalChunks=d.manifestFlushIntervalChunks;o.finalVerifyPolicy=d.finalVerifyPolicy;
    o.commitSyncPolicy=d.commitSyncPolicy;o.preallocateMode=d.preallocateMode;o.fileIo=d.fileIo;
    o.dataTls=d.dataTls;o.dataTlsMode=d.dataTlsMode;o.runtimeMetrics=d.runtimeMetrics;
    o.hotPathCompression=d.hotPathCompression;o.overwrite=d.overwrite;o.resume=d.resume;o.maxChunks=d.maxChunks;
    return receiveUnifiedFile(o,true);
}
}
