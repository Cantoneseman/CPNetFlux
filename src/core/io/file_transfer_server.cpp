#include "cpnetflux/core/io/file_transfer_server.h"
#include "cpnetflux/core/io/unified_file_transfer.h"
namespace cpnetflux::core::io {
common::Status runFileTransferServerOnListener(const config::FileTransferOptions& o,UniqueFd listener,int controlFd) {
    return receiveUnifiedFile(o,false,std::move(listener),controlFd);
}
common::Status runFileTransferServer(const config::FileTransferOptions& o) {
    auto exists=storage::PosixFile::pathExists(o.path);
    if (!exists.isOk()) return exists.status();
    if (exists.value() && !o.overwrite)
        return common::Status::invalidArgument("output file already exists; use --overwrite");
    auto listener=createListener(o.host.c_str(),o.port,128);
    if (!listener.isOk()) return listener.status();
    return runFileTransferServerOnListener(o,std::move(listener.value()));
}
}
