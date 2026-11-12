#pragma once

#include <string_view>

#include "cpnetflux/common/status.h"

namespace cpnetflux::core::io {

enum class DataTransport {
    Tcp,
    Rdma,
};

[[nodiscard]] common::Result<DataTransport> parseDataTransport(std::string_view value);
[[nodiscard]] const char* dataTransportName(DataTransport transport) noexcept;

}  // namespace cpnetflux::core::io
