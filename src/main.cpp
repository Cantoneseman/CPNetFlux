#include "gridflux/version.h"

#include <spdlog/spdlog.h>

int main() {
    spdlog::info("{} {}", gridflux::projectName(), gridflux::projectVersion());
    return 0;
}
