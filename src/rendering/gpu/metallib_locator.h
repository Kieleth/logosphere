#pragma once

// Resolve default.metallib relative to the RUNNING EXECUTABLE, never the
// current working directory. CWD-relative search served debug-tree shaders
// to Release binaries whenever eden ran from the repo root — stale kernels,
// silently (GPU_PIPELINE_AUDIT_2026-07.md, "metallib load-path hazard").
// Walks up from the executable's directory checking the layouts CMake
// produces (build root, standalone subdir, FetchContent consumer).

#include <mach-o/dyld.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string>

namespace logosphere { namespace gpu {

inline std::string locate_metallib() {
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return {};
    char real[PATH_MAX];
    if (!realpath(buf, real)) return {};
    std::string dir(real);
    auto slash = dir.rfind('/');
    if (slash == std::string::npos) return {};
    dir.resize(slash);  // executable's directory

    const char* candidates[] = {
        "default.metallib",
        "logosphere/default.metallib",
        "_deps/logosphere-build/default.metallib",
    };
    for (int up = 0; up < 4; ++up) {
        for (const char* rel : candidates) {
            std::string p = dir + "/" + rel;
            struct stat st;
            if (stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode)) return p;
        }
        auto s = dir.rfind('/');
        if (s == std::string::npos || s == 0) break;
        dir.resize(s);
    }
    return {};
}

} }  // namespace logosphere::gpu
