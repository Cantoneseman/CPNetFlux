#include "gridflux/version.h"

namespace gridflux {

std::string_view projectName() noexcept {
    return kProjectName;
}

std::string_view projectVersion() noexcept {
    return kProjectVersion;
}

}  // namespace gridflux
