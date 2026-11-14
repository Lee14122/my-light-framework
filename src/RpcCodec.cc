#include "myframework/rpc/RpcCodec.h"
#include "myframework/Logger.h"
#include <cstring>
#include <arpa/inet.h>

namespace myframework {

RpcCodec::RpcCodec() {
    serializer_ = std::make_unique<JsonSerializer>();
}

RpcCodec::~RpcCodec() = default;

void RpcCodec::setSerializer(std::unique_ptr<Serializer> s) {
    serializer_ = std::move(s);
}

void RpcCodec::setMessageCallback(MessageCallback cb) {
    messageCallback_ = std::move(cb);
}

// ========== 编码：RpcMessage → 网络字节流 ==========

std::string RpcCodec::encode(const RpcMessage& msg) {
    // 先计算长度
    RpcMessage m = msg;
    m.computeLengths();

    std::string result;
    result.reserve(m.totalLen);
    result.resize(m.totalLen);

    char* p = &result[0];

    // ---- 定长帧头 (28 字节, 大端) ----
    uint32_t magic_be    = htonl(m.magic);
    uint32_t totalLen_be = htonl(m.totalLen);
    uint16_t headerLen_be = htons(m.headerLen);
    uint32_t seqId_be    = htonl(m.seqId);
    uint16_t serviceLen_be = htons(m.serviceLen);
    uint16_t methodLen_be  = htons(m.methodLen);
    uint16_t metaLen_be    = htons(m.metaLen);
    uint32_t payloadLen_be = htonl(m.payloadLen);

    size_t offset = 0;
    memcpy(p + offset, &magic_be, 4);        offset += 4;
    memcpy(p + offset, &totalLen_be, 4);     offset += 4;
    memcpy(p + offset, &headerLen_be, 2);    offset += 2;
    p[offset] = m.type;                      offset += 1;   // 1 byte, 无需字节序
    p[offset] = m.compress;                  offset += 1;   // 1 byte
    memcpy(p + offset, &seqId_be, 4);        offset += 4;
    memcpy(p + offset, &serviceLen_be, 2);   offset += 2;
    memcpy(p + offset, &methodLen_be, 2);    offset += 2;
    memcpy(p + offset, &metaLen_be, 2);      offset += 2;
    memcpy(p + offset, &payloadLen_be, 4);   offset += 4;
    // offset 现在应为 28

    // ---- 可变长字段 ----
    memcpy(p + offset, m.serviceName.data(), m.serviceName.size());
    offset += m.serviceName.size();
    memcpy(p + offset, m.methodName.data(), m.methodName.size());
    offset += m.methodName.size();
    memcpy(p + offset, m.meta.data(), m.meta.size());
    offset += m.meta.size();
    memcpy(p + offset, m.payload.data(), m.payload.size());

    return result;
}

// ========== 解码：网络字节流 → RpcMessage ==========

bool RpcCodec::decode(const char* data, size_t len, RpcMessage& msg) {
    if (len < RpcMessage::HEADER_SIZE) {
        return false;
    }

    size_t offset = 0;

    // 读帧头（大端 → 主机序）
    uint32_t magic_be, totalLen_be, seqId_be, payloadLen_be;
    uint16_t headerLen_be, serviceLen_be, methodLen_be, metaLen_be;

    memcpy(&magic_be, data + offset, 4);        offset += 4;
    memcpy(&totalLen_be, data + offset, 4);     offset += 4;
    memcpy(&headerLen_be, data + offset, 2);    offset += 2;
    msg.type    = static_cast<uint8_t>(data[offset]);  offset += 1;
    msg.compress = static_cast<uint8_t>(data[offset]); offset += 1;
    memcpy(&seqId_be, data + offset, 4);        offset += 4;
    memcpy(&serviceLen_be, data + offset, 2);   offset += 2;
    memcpy(&methodLen_be, data + offset, 2);    offset += 2;
    memcpy(&metaLen_be, data + offset, 2);      offset += 2;
    memcpy(&payloadLen_be, data + offset, 4);   offset += 4;

    msg.magic      = ntohl(magic_be);
    msg.totalLen   = ntohl(totalLen_be);
    msg.headerLen  = ntohs(headerLen_be);
    msg.seqId      = ntohl(seqId_be);
    msg.serviceLen = ntohs(serviceLen_be);
    msg.methodLen  = ntohs(methodLen_be);
    msg.metaLen    = ntohs(metaLen_be);
    msg.payloadLen = ntohl(payloadLen_be);

    // 检查数据是否足够
    if (len < msg.totalLen) {
        return false;
    }

    // 读可变长字段
    if (msg.serviceLen > 0) {
        msg.serviceName.assign(data + offset, msg.serviceLen);
        offset += msg.serviceLen;
    }
    if (msg.methodLen > 0) {
        msg.methodName.assign(data + offset, msg.methodLen);
        offset += msg.methodLen;
    }
    if (msg.metaLen > 0) {
        msg.meta.assign(data + offset, msg.metaLen);
        offset += msg.metaLen;
    }
    if (msg.payloadLen > 0) {
        msg.payload.assign(data + offset, msg.payloadLen);
        offset += msg.payloadLen;
    }

    return msg.validate();
}

// ========== 从 TCP 字节流中拆帧 ==========

void RpcCodec::onData(Buffer* buf) {
    while (buf->readableBytes() >= RpcMessage::HEADER_SIZE) {
        // 先读帧头获取 totalLen
        const char* peekData = buf->peek();

        // 解析 totalLen（跳过 magic 4 字节，读 totalLen 4 字节）
        uint32_t totalLen_be;
        memcpy(&totalLen_be, peekData + 4, 4);
        uint32_t totalLen = ntohl(totalLen_be);

        // 帧长度合法性检查
        if (totalLen < RpcMessage::HEADER_SIZE || totalLen > 64 * 1024 * 1024) {
            MFLOG_ERROR("RpcCodec", "Invalid frame totalLen=%u, discarding connection", totalLen);
            buf->retrieveAll();
            return;
        }

        // 数据不够一帧，等待更多数据
        if (buf->readableBytes() < totalLen) {
            return;
        }

        // 有完整帧，提取数据并解码
        std::string frameData(peekData, totalLen);
        buf->retrieve(totalLen);

        RpcMessage msg;
        if (decode(frameData.data(), frameData.size(), msg)) {
            if (messageCallback_) {
                messageCallback_(msg);
            }
        } else {
            MFLOG_WARN("RpcCodec", "Failed to decode message, magic=0x%x", msg.magic);
        }
    }
}

// ========== 快捷构造方法 ==========

RpcMessage RpcCodec::makeRequest(uint32_t seqId,
                                  const std::string& service,
                                  const std::string& method,
                                  const std::string& payload) {
    RpcMessage msg;
    msg.type        = RpcMessage::REQUEST;
    msg.seqId       = seqId;
    msg.serviceName = service;
    msg.methodName  = method;
    msg.payload     = payload;
    msg.computeLengths();
    return msg;
}

RpcMessage RpcCodec::makeResponse(uint32_t seqId, const std::string& payload) {
    RpcMessage msg;
    msg.type    = RpcMessage::RESPONSE;
    msg.seqId   = seqId;
    msg.payload = payload;
    msg.computeLengths();
    return msg;
}

RpcMessage RpcCodec::makeError(uint32_t seqId, const std::string& errorPayload) {
    RpcMessage msg;
    msg.type    = RpcMessage::ERROR;
    msg.seqId   = seqId;
    msg.payload = errorPayload;
    msg.computeLengths();
    return msg;
}

RpcMessage RpcCodec::makeHeartbeat() {
    RpcMessage msg;
    msg.type = RpcMessage::HEARTBEAT;
    msg.computeLengths();
    return msg;
}

} // namespace myframework