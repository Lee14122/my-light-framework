/**
 * test_codec_roundtrip.cpp
 *
 * 协议 Round-Trip 单元测试：
 *   1. 普通请求帧 encode→decode 往返验证
 *   2. 空 payload 心跳帧长度/类型验证
 *   3. 多帧拼接（防粘包）按 totalLen 逐帧切分验证
 *   4. 非法篡改（totalLen=0）安全返回 false 且不崩溃
 */

#include "myframework/rpc/RpcCodec.h"
#include <cstring>
#include <iostream>
#include <arpa/inet.h>

using namespace myframework;

static int passed = 0;
static int failed = 0;

#define TEST(name) \
    std::cout << "\n--- Test: " << name << " ---\n"

#define CHECK(cond, msg) do { \
    if (cond) { \
        std::cout << "  ✅ " << msg << "\n"; \
        ++passed; \
    } else { \
        std::cout << "  ❌ " << msg << "  [FAIL]\n"; \
        ++failed; \
    } \
} while(0)

// ============================================================================
// Test 1: 普通请求帧 encode → decode 往返
// ============================================================================
void test_normal_request_roundtrip() {
    TEST("normal request round-trip");

    // 构造请求
    RpcMessage orig;
    orig.type        = RpcMessage::REQUEST;
    orig.seqId       = 42;
    orig.serviceName = "UserService";
    orig.methodName  = "GetUser";
    orig.meta        = "trace-id:abc123";
    orig.payload     = R"({"userId":100})";
    orig.computeLengths();

    // encode
    std::string wire = RpcCodec::encode(orig);

    // 验证帧总长度
    size_t expectedLen = RpcMessage::HEADER_SIZE
                       + orig.serviceName.size()
                       + orig.methodName.size()
                       + orig.meta.size()
                       + orig.payload.size();
    CHECK(wire.size() == expectedLen,
          "encoded size matches HEADER_SIZE + variable fields");

    // decode
    RpcMessage decoded;
    bool ok = RpcCodec::decode(wire.data(), wire.size(), decoded);
    CHECK(ok, "decode returned true");

    // 逐字段比对
    CHECK(decoded.magic      == RpcMessage::MAGIC,      "magic matches");
    CHECK(decoded.totalLen   == orig.totalLen,           "totalLen matches");
    CHECK(decoded.headerLen  == RpcMessage::HEADER_SIZE, "headerLen matches");
    CHECK(decoded.type       == RpcMessage::REQUEST,     "type matches (REQUEST)");
    CHECK(decoded.compress   == RpcMessage::NONE,        "compress matches (NONE)");
    CHECK(decoded.checksum   == 0,                       "checksum is 0");
    CHECK(decoded.seqId      == 42,                      "seqId matches");
    CHECK(decoded.serviceLen == orig.serviceLen,          "serviceLen matches");
    CHECK(decoded.methodLen  == orig.methodLen,           "methodLen matches");
    CHECK(decoded.metaLen    == orig.metaLen,             "metaLen matches");
    CHECK(decoded.payloadLen == orig.payloadLen,          "payloadLen matches");

    CHECK(decoded.serviceName == "UserService",           "serviceName matches");
    CHECK(decoded.methodName  == "GetUser",               "methodName matches");
    CHECK(decoded.meta        == "trace-id:abc123",       "meta matches");
    CHECK(decoded.payload     == R"({"userId":100})",     "payload matches");
}

// ============================================================================
// Test 2: 空 payload 心跳帧
// ============================================================================
void test_heartbeat_empty_payload() {
    TEST("heartbeat empty payload");

    RpcMessage hb = RpcCodec::makeHeartbeat();
    std::string wire = RpcCodec::encode(hb);

    CHECK(wire.size() == RpcMessage::HEADER_SIZE,
          "heartbeat frame has exactly HEADER_SIZE bytes");

    RpcMessage decoded;
    bool ok = RpcCodec::decode(wire.data(), wire.size(), decoded);
    CHECK(ok, "heartbeat decode returned true");

    CHECK(decoded.type       == RpcMessage::HEARTBEAT, "type is HEARTBEAT");
    CHECK(decoded.payloadLen == 0,                     "payloadLen is 0");
    CHECK(decoded.serviceLen == 0,                     "serviceLen is 0");
    CHECK(decoded.methodLen  == 0,                     "methodLen is 0");
    CHECK(decoded.payload.empty(),                     "payload string is empty");
}

// ============================================================================
// Test 3: 多帧拼接（模拟 TCP 粘包，按 totalLen 逐帧拆包）
// ============================================================================
void test_multi_frame_splicing() {
    TEST("multi-frame splicing (anti-sticky-packet)");

    // 构造 3 帧，seqId 分别为 100, 200, 300
    RpcMessage m1 = RpcCodec::makeRequest(100, "SvcA", "Method1", "payload1");
    RpcMessage m2 = RpcCodec::makeResponse(200, "payload2");
    RpcMessage m3 = RpcCodec::makeError(300, R"({"code":-1})");

    std::string w1 = RpcCodec::encode(m1);
    std::string w2 = RpcCodec::encode(m2);
    std::string w3 = RpcCodec::encode(m3);

    // 拼接成一个连续的字节流
    std::string merged = w1 + w2 + w3;
    CHECK(merged.size() == w1.size() + w2.size() + w3.size(),
          "merged size equals sum of individual frame sizes");

    // 模拟按 totalLen 拆包
    const char* data = merged.data();
    size_t remaining = merged.size();
    uint32_t expectedSeqIds[] = {100, 200, 300};
    int frameIdx = 0;

    while (remaining >= RpcMessage::HEADER_SIZE && frameIdx < 3) {
        // 从帧头读取 totalLen
        uint32_t totalLen_be;
        memcpy(&totalLen_be, data, 4); // skip magic, read totalLen

        // Wait, totalLen starts at offset 4. Let me re-read the wire format:
        // offset 0-3: magic (4 bytes)
        // offset 4-7: totalLen (4 bytes)
        // ... so totalLen is at offset 4
        // But we need to get the actual value.
        // Actually the format is: magic(4), totalLen(4), headerLen(2), type(1), compress(1),
        // checksum(2), seqId(4), ...
        // So totalLen_be is at offset 4.
        // Let me redo this correctly.
        uint32_t totalLen;
        memcpy(&totalLen_be, data + 4, 4);
        totalLen = ntohl(totalLen_be);

        if (totalLen < RpcMessage::HEADER_SIZE || totalLen > remaining) {
            CHECK(false, "invalid totalLen in multi-frame test");
            return;
        }

        RpcMessage decoded;
        bool ok = RpcCodec::decode(data, totalLen, decoded);
        CHECK(ok, ("decode frame[" + std::to_string(frameIdx) + "] returned true").c_str());

        CHECK(decoded.seqId == expectedSeqIds[frameIdx],
              ("frame[" + std::to_string(frameIdx)
               + "] seqId matches: expected "
               + std::to_string(expectedSeqIds[frameIdx])
               + ", got " + std::to_string(decoded.seqId)).c_str());

        data      += totalLen;
        remaining -= totalLen;
        ++frameIdx;
    }

    CHECK(frameIdx == 3, "all 3 frames decoded successfully");
}

// ============================================================================
// Test 4: 非法篡改 — totalLen 改为 0，验证不崩溃且返回 false
// ============================================================================
void test_corrupted_totalLen() {
    TEST("corrupted totalLen (zero) — should not crash");

    // 构造一个合法帧
    RpcMessage orig = RpcCodec::makeRequest(99, "Test", "Foo", "data");
    std::string wire = RpcCodec::encode(orig);

    // 篡改 totalLen（偏移 4-7，大端写入 0）
    uint32_t zero_be = htonl(0);
    memcpy(&wire[4], &zero_be, 4);

    RpcMessage decoded;
    bool ok = RpcCodec::decode(wire.data(), wire.size(), decoded);
    CHECK(!ok, "decode returns false on zero totalLen (no crash)");

    // 再多测一种：totalLen 设为比帧头还小的值（如 10）
    RpcMessage orig2 = RpcCodec::makeRequest(77, "Svc", "M", "x");
    std::string wire2 = RpcCodec::encode(orig2);
    uint32_t small_be = htonl(10);
    memcpy(&wire2[4], &small_be, 4);

    RpcMessage decoded2;
    bool ok2 = RpcCodec::decode(wire2.data(), wire2.size(), decoded2);
    CHECK(!ok2, "decode returns false on totalLen < HEADER_SIZE (no crash)");
}

// ============================================================================
// main
// ============================================================================
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  RPC Protocol Round-Trip Unit Tests    " << std::endl;
    std::cout << "========================================" << std::endl;

    test_normal_request_roundtrip();
    test_heartbeat_empty_payload();
    test_multi_frame_splicing();
    test_corrupted_totalLen();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Passed: " << passed << " / " << (passed + failed) << std::endl;
    std::cout << "  Failed: " << failed << std::endl;
    std::cout << "========================================" << std::endl;

    if (failed == 0) {
        std::cout << "🎉 All round-trip tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "💥 Some tests FAILED!" << std::endl;
        return 1;
    }
}