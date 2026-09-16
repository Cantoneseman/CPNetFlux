#include "cpnetflux/version.h"

namespace cpnetflux {

std::string_view projectName() noexcept {
    return kProjectName;
}

std::string_view projectVersion() noexcept {
    return kProjectVersion;
}

}  // namespace cpnetflux
