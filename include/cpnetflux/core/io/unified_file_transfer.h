#pragma once
#include "cpnetflux/core/io/file_transfer_server.h"
#include "cpnetflux/core/io/file_download_sender.h"
#include "cpnetflux/config/file_download_options.h"
namespace cpnetflux::core::io {
common::Status sendUnifiedFile(const config::FileTransferOptions&, UniqueFd listener = {}, int controlFd = -1);
common::Status receiveUnifiedFile(const config::FileTransferOptions&, bool download,
                                 UniqueFd listener = {}, int controlFd = -1);
}
