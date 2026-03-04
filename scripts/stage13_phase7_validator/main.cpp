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
        std::cerr << "[stage13_validator] FAIL: " << message << "\n";
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
    ok &= check(decodedFrame.deserialize(framePayload), "SensorFrame deserialize should succeed");
    ok &= check(decodedFrame.color == P7::Color::Green, "enum round-trip should match");
    ok &= check(decodedFrame.samples.size() == 4, "sequence<int32,8> size should match");
    ok &= check(decodedFrame.tags.size() == 2, "sequence<string> size should match");
    ok &= check(decodedFrame.tags[1] == "beta", "sequence<string> value should match");

    P7::Value v1;
    v1.discriminator = 1;
    v1.text = "hello";
    const std::vector<uint8_t> v1Payload = v1.serialize();

    P7::Value v1Decoded;
    ok &= check(v1Decoded.deserialize(v1Payload), "Value deserialize(case 1) should succeed");
    ok &= check(v1Decoded.discriminator == 1, "union discriminator should match case1");
    ok &= check(v1Decoded.text == "hello", "union string case should match");

    P7::Value vDefault;
    vDefault.discriminator = 9;
    vDefault.raw = {7, 8, 9};
    const std::vector<uint8_t> vDefaultPayload = vDefault.serialize();

    P7::Value vDefaultDecoded;
    ok &= check(vDefaultDecoded.deserialize(vDefaultPayload), "Value deserialize(default) should succeed");
    ok &= check(vDefaultDecoded.discriminator == 9, "union default discriminator should match");
    ok &= check(vDefaultDecoded.raw.size() == 3, "union default sequence size should match");
    ok &= check(vDefaultDecoded.raw[2] == 9, "union default sequence value should match");

    std::cout << "[stage13_validator] result=" << (ok ? "ok" : "fail") << "\n";
    return ok ? 0 : 1;
}
