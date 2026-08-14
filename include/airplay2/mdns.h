#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gwl::airplay2 {

struct MdnsTxtRecord {
    std::string key;
    std::string value;
};

class MdnsService {
public:
    MdnsService();
    ~MdnsService();

    MdnsService(const MdnsService&) = delete;
    MdnsService& operator=(const MdnsService&) = delete;

    bool publish(const std::string& instance_name,
                 std::uint16_t port,
                 const std::vector<MdnsTxtRecord>& records);
    void unpublish();
    bool published() const noexcept;

private:
    class Impl;
    Impl* impl_;
};

} // namespace gwl::airplay2
