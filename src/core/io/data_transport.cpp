#include "cpnetflux/core/io/data_transport.h"

namespace cpnetflux::core::io {

common::Result<DataTransport> parseDataTransport(std::string_view value) {
    if (value == "tcp") return DataTransport::Tcp;
    if (value == "rdma") return DataTransport::Rdma;
    return common::Status::invalidArgument("data transport must be tcp or rdma");
}

const char* dataTransportName(DataTransport transport) noexcept {
    return transport == DataTransport::Rdma ? "rdma" : "tcp";
}

}  // namespace cpnetflux::core::io
