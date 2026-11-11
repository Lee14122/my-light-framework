#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace myframework {

/**
 * RPC 二进制协议帧格式（高性能设计）
 *
 * ┌──────────┬──────────┬──────────┬──────────┬──────────┐
 * │  magic   │ totalLen │ hdrLen   │  type    │ compress │
 * │ 4 bytes  │ 4 bytes  │ 2 bytes  │ 1 byte   │ 1 byte   │
 * ├──────────┼──────────┼──────────┼──────────┼──────────┤
 * │  seqId   │ svcNameLen│ methNameLen│ metaLen │  payloadLen│
 * │ 4 bytes  │ 2 bytes   │ 2 bytes   │ 2 bytes │ 4 bytes    │
 * ├──────────┴──────────┴──────────┴──────────┴──────────┤
 * │   service_name [svcNameLen]                           │
 * │   method_name  [methNameLen]                          │
 * │   meta         [metaLen]   (透传字段，如 trace-id)      │
 * │   payload      [payloadLen]                           │
 * └───────────────────────────────────────────────────────┘
 *
 * 帧头固定 28 字节，后跟可变长的 service/method/meta/payload
 */
struct RpcMessage {
    static constexpr uint32_t MAGIC      = 0xABCD1234;
    static constexpr size_t   HEADER_SIZE = 28;

    // ---- 消息类型 ----
    enum Type : uint8_t {
        REQUEST   = 1,   // 服务调用请求
        RESPONSE  = 2,   // 调用成功响应
        ERROR     = 3,   // 调用错误响应
        HEARTBEAT = 4,   // 心跳（保活）
    };

    // ---- 压缩算法 ----
    enum Compression : uint8_t {
        NONE = 0,
    };

    // ---- 帧头字段 ----
    uint32_t magic        = MAGIC;      // 魔数
    uint32_t totalLen     = 0;          // 帧总长度（含帧头）
    uint16_t headerLen    = HEADER_SIZE;
    uint8_t  type         = REQUEST;
    uint8_t  compress     = NONE;
    uint32_t seqId        = 0;          // 序列号，用于请求-响应匹配
    uint16_t serviceLen   = 0;
    uint16_t methodLen    = 0;
    uint16_t metaLen      = 0;
    uint32_t payloadLen   = 0;

    // ---- 可变长字段 ----
    std::string serviceName;             // 服务名（如 "UserService"）
    std::string methodName;              // 方法名（如 "GetUser"）
    std::string meta;                    // 元数据/透传头（如 trace-id, timeout）
    std::string payload;                 // 序列化后的业务数据

    // ---- 工具函数 ----
    void computeLengths();
    bool validate() const;               // 校验魔数和长度
};

} // namespace myframework