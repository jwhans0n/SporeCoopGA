#pragma once
#include "net/protocol.h"

namespace editor_traffic_gate {
// Authoring owns its run/project channels. Session metadata stays available;
// campaign world changes and gameplay replication wait for authoring to end.
inline bool allowed(uint8_t type){
    switch(type){
    case net::MSG_DIAG: case net::MSG_EDITOR: case net::MSG_ADVENTURE:
    case net::MSG_ACTIVITY: case net::MSG_LOBBY: case net::MSG_SETTINGS:
    case net::MSG_COLORS: case net::MSG_REPORT:return true;
    default:return false;
    }
}
}
