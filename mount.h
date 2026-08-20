#pragma once

#include <string>

class MountManager final
{
public:
    MountManager(const std::string& sharedFolderPath,
                 const std::string& mountPoint,
                 const std::string& username,
                 const std::string& password,
                 const std::string& logFileName,
                 const std::string& logOption,
                 const std::string& extraMountOptions = {});

    ~MountManager();

    MountManager(const MountManager&) = delete;
    MountManager& operator=(const MountManager&) = delete;
    MountManager(MountManager&&) = delete;
    MountManager& operator=(MountManager&&) = delete;

    bool isMounted() const;

private:
    bool acquireMount(const std::string& username,
                      const std::string& password,
                      const std::string& extraMountOptions);
    void releaseMount();

    std::string sharedFolderPath_;
    std::string mountPoint_;
    std::string logFileName_;
    std::string logOption_;

    bool mounted_{false};
    bool leaseAcquired_{false};
};
