#include "cpnetflux/core/io/file_transfer_client.h"
#include "cpnetflux/core/io/unified_file_transfer.h"
namespace cpnetflux::core::io {
common::Status runFileTransferClient(const config::FileTransferOptions& o) { return sendUnifiedFile(o); }
}
