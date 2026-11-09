#include "env_source.h"

#include <cstdio>
#include <cstdlib>
#include <limits.h>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

static bool exists(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0;
}

std::string LocalEnvSource::resolve(const std::string& ref) {
    char canon[PATH_MAX];
    if (!realpath(ref.c_str(), canon)) return "";
    return exists(canon) ? std::string(canon) : "";
}

NixEnvSource::NixEnvSource(std::string roots_dir)
    : roots_dir(std::move(roots_dir)) {
    if (roots_dir.empty()) roots_dir = ".minrt-roots";
    std::error_code ec;
    std::filesystem::create_directories(roots_dir, ec);
    if (ec)
        fprintf(stderr, "NixEnvSource: roots dir '%s': %s\n",
                roots_dir.c_str(), ec.message().c_str());
}

std::string NixEnvSource::resolve(const std::string& ref) {
    char canon[PATH_MAX];

    std::string store_path = realpath(ref.c_str(), canon) ? canon : ref;
    const std::string so = store_path + "/lib/env.so";

    if (exists(so)) return so;
    return fetch(store_path, so);
}

std::string NixEnvSource::fetch(const std::string& store_path,
                                const std::string& so) {
    std::string base = store_path.substr(store_path.find_last_of('/') + 1);
    std::string cmd = "nix-store --realise '" + store_path +
                      "' --add-root '" + roots_dir + "/" + base +
                      "' >/dev/null";
    if (system(cmd.c_str()) != 0) {
        fprintf(stderr, "NixEnvSource: realise failed for %s\n",
                store_path.c_str());
        return "";
    }
    if (!exists(so))
        fprintf(stderr, "NixEnvSource: %s present but lacks lib/env.so\n",
                store_path.c_str());
    return exists(so) ? so : "";
}
