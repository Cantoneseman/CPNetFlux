#include "cpnetflux/version.h"

#include <spdlog/spdlog.h>

int main() {
    spdlog::info("{} {}", cpnetflux::projectName(), cpnetflux::projectVersion());
    return 0;
}
