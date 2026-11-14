#include "myframework/rpc/RpcMessage.h"
#include <cstring>

namespace myframework {

void RpcMessage::computeLengths() {
    serviceLen = static_cast<uint16_t>(serviceName.size());
    methodLen  = static_cast<uint16_t>(methodName.size());
    metaLen    = static_cast<uint16_t>(meta.size());
    payloadLen = static_cast<uint32_t>(payload.size());
    totalLen   = HEADER_SIZE + serviceLen + methodLen + metaLen + payloadLen;
}

bool RpcMessage::validate() const {
    if (magic != MAGIC) return false;
    if (headerLen != HEADER_SIZE) return false;
    size_t expected = HEADER_SIZE + serviceLen + methodLen + metaLen + payloadLen;
    if (totalLen != expected) return false;
    return true;
}

} // namespace myframework