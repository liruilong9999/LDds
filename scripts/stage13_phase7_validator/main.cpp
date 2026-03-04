#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "phase7_sample_define.h"

namespace {

bool check(bool condition, const std::string & message)
{
    if (!condition)
    {
        std::cerr << "[stage13_validator] FAIL(失败): " << message << "\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    bool ok = true;

    P7::SensorFrame frame;
    frame.color = P7::Color::Green;
    frame.samples = {1, 2, 3, 4};
    frame.tags = {"alpha", "beta"};

    const std::vector<uint8_t> framePayload = frame.serialize();
    P7::SensorFrame decodedFrame;
    ok &= check(decodedFrame.deserialize(framePayload), "SensorFrame 反序列化应成功");
    ok &= check(decodedFrame.color == P7::Color::Green, "enum 往返结果应一致");
    ok &= check(decodedFrame.samples.size() == 4, "sequence<int32,8> 长度应一致");
    ok &= check(decodedFrame.tags.size() == 2, "sequence<string> 长度应一致");
    ok &= check(decodedFrame.tags[1] == "beta", "sequence<string> 值应一致");

    P7::Value v1;
    v1.discriminator = 1;
    v1.text = "hello";
    const std::vector<uint8_t> v1Payload = v1.serialize();

    P7::Value v1Decoded;
    ok &= check(v1Decoded.deserialize(v1Payload), "Value 反序列化(case 1)应成功");
    ok &= check(v1Decoded.discriminator == 1, "union 判别字段应匹配 case1");
    ok &= check(v1Decoded.text == "hello", "union 字符串分支值应匹配");

    P7::Value vDefault;
    vDefault.discriminator = 9;
    vDefault.raw = {7, 8, 9};
    const std::vector<uint8_t> vDefaultPayload = vDefault.serialize();

    P7::Value vDefaultDecoded;
    ok &= check(vDefaultDecoded.deserialize(vDefaultPayload), "Value 反序列化(default)应成功");
    ok &= check(vDefaultDecoded.discriminator == 9, "union default 判别字段应匹配");
    ok &= check(vDefaultDecoded.raw.size() == 3, "union default sequence 长度应匹配");
    ok &= check(vDefaultDecoded.raw[2] == 9, "union default sequence 值应匹配");

    std::cout << "[stage13_validator] result=" << (ok ? "ok" : "fail")
              << " 说明=" << (ok ? "通过" : "失败") << "\n";
    return ok ? 0 : 1;
}
