#include <string>

namespace gwl::airplay2 {

// mDNS/DNS-SD publication is intentionally isolated here. The next protocol
// phase will provide the portable multicast-DNS implementation and AirPlay
// TXT records without coupling the receiver to a platform-specific Bonjour
// or Avahi API.
class MdnsServiceState {
public:
    std::string service_type = "_airplay._tcp.local";
};

} // namespace gwl::airplay2
