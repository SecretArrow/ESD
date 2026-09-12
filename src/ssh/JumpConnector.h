#pragma once

#include <memory>
#include <vector>

#include "../core/profiles/ConnectionProfile.h"
#include "Bridge.h"
#include "Types.h"

namespace eclipse {

// ---------------------------------------------------------------------------
// JumpConnector - builds a jump host / bastion chain:
//
//   Laptop -> bastion1 -> bastion2 -> target
//
// Each hop opens a direct-tcpip channel on the previous hop; a Bridge (two
// pump threads + socket pair) feeds the next SSH engine through it. The
// chain object must stay alive for the whole session (pumps carry traffic).
//
// Security policy: hop host keys are verified against known_hosts. Unknown
// hop keys abort with a clear message (connect to the bastion once directly);
// changed keys always abort.
// ---------------------------------------------------------------------------
class JumpConnector
{
public:
    JumpConnector() = default;
    ~JumpConnector();

    bool establishChain(const ConnectionProfile& profile, int& outFd, QString* err);

private:
    struct Hop
    {
        QString label;
    };

    std::vector<std::unique_ptr<Bridge>> m_bridges;
    std::vector<QString> m_hopLabels;
};

} // namespace eclipse
