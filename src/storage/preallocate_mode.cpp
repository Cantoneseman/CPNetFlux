#include "gridflux/storage/preallocate_mode.h"

namespace gridflux::storage {

common::Result<PreallocateMode> parsePreallocateMode(std::string_view text) {
    if (text == "off") {
        return PreallocateMode::Off;
    }
    if (text == "full") {
        return PreallocateMode::Full;
    }
    return common::Status::invalidArgument("preallocate mode must be off or full");
}

std::string preallocateModeName(PreallocateMode mode) {
    switch (mode) {
        case PreallocateMode::Off:
            return "off";
        case PreallocateMode::Full:
            return "full";
    }
    return "off";
}

}  // namespace gridflux::storage
