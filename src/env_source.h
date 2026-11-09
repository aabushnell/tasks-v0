#pragma once

#include <string>

class EnvSource {
public:
    virtual ~EnvSource() = default;
    // Resolve ref to an existing .so path. May block (fetch).
    // Returns "" on failure.
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
    std::string roots_dir_;   // GC-root links live here for the session
};
