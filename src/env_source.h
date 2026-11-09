#pragma once

#include <memory>
#include <string>

class EnvSource {
public:
    virtual ~EnvSource() = default;
    virtual std::string resolve(const std::string& ref) = 0;
};

class LocalEnvSource : public EnvSource {
public:
    std::string resolve(const std::string& ref) override;
};

class NixEnvSource : public EnvSource {
public:
    explicit NixEnvSource(std::string roots_dir);
    std::string resolve(const std::string& ref) override;
private:
    std::string fetch(const std::string& store_path, const std::string& so);
    std::string roots_dir;
};

inline std::unique_ptr<EnvSource> mk_env_source(const std::string& kind) {
    if (kind == "nix")
        return std::make_unique<NixEnvSource>(".minrt-roots");
    return std::make_unique<LocalEnvSource>();
}
