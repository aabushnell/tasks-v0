// env_source.cpp — see env_source.hpp.
// NOTE: NixEnvSource is compiled but NOT covered by the sandbox demo
// (no nix available there). Verify on a nix host before trusting it.
#include "env_source.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    : roots_dir_(std::move(roots_dir)) {}

std::string NixEnvSource::resolve(const std::string& ref) {
    // Convention: the env's loadable artifact is <store-path>/lib/env.so.
    const std::string so = ref + "/lib/env.so";
    if (exists(so)) return so;   // already in the local store

    // Not local: realise it (substitutes from configured binary caches)
    // and pin a GC root for it in the same step, so the path cannot be
    // collected under running tasks. Refs come from good-faith users;
    // still, quote the shell args.
    std::string base = ref.substr(ref.find_last_of('/') + 1);
    std::string cmd = "nix-store --realise '" + ref +
                      "' --add-root '" + roots_dir_ + "/" + base + "' >/dev/null";
    if (system(cmd.c_str()) != 0) {
        fprintf(stderr, "NixEnvSource: realise failed for %s\n", ref.c_str());
        return "";
    }
    return exists(so) ? so : "";
}
