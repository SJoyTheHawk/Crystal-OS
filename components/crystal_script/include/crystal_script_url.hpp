/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

// Host-only manifest policy for the script HTTP bridge. Reject userinfo and
// malformed authorities before matching an exact allowlisted DNS name.
inline bool crystal_script_host_allowed(const std::string &url,
                                        const std::vector<std::string> &hosts)
{
    const size_t scheme = url.find("://");
    if (scheme == std::string::npos ||
        !((scheme == 5 && url.compare(0, scheme, "https") == 0) ||
          (scheme == 4 && url.compare(0, scheme, "http") == 0))) return false;

    const size_t start = scheme + 3;
    const size_t end = url.find_first_of("/?#", start);
    const std::string authority = url.substr(start, end == std::string::npos ? end : end - start);
    if (authority.empty() || authority.find('@') != std::string::npos ||
        authority.find('\\') != std::string::npos) return false;

    std::string host = authority;
    const size_t port = authority.find(':');
    if (port != std::string::npos) {
        if (port == 0 || authority.find(':', port + 1) != std::string::npos || port + 1 == authority.size()) return false;
        uint32_t port_number = 0;
        for (size_t i = port + 1; i < authority.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(authority[i]);
            if (c < '0' || c > '9' || port_number > 6553) return false;
            port_number = port_number * 10 + (c - '0');
        }
        if (!port_number || port_number > 65535) return false;
        host.resize(port);
    }
    if (host.empty() || host.size() > 253) return false;
    std::transform(host.begin(), host.end(), host.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::find(hosts.begin(), hosts.end(), host) != hosts.end();
}
