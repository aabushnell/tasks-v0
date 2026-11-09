#include "env_source.h"

#include <cstdio>
#include <cstdlib>
#include <limits.h>
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
    : roots_dir(std::move(roots_dir)) {}

std::string NixEnvSource::resolve(const std::string& ref) {
    // location: <store-path>/lib/env.so.
    const std::string so = ref + "/lib/env.so";
    if (exists(so)) return so;

    std::string base = ref.substr(ref.find_last_of('/') + 1);
    std::string cmd = "nix-store --realise '" + ref +
                      "' --add-root '" + roots_dir + "/" + base + "' >/dev/null";
    if (system(cmd.c_str()) != 0) {
        fprintf(stderr, "NixEnvSource: realise failed for %s\n", ref.c_str());
        return "";
    }
    return exists(so) ? so : "";
}
