#include "file1_topic.h"

namespace LDdsFramework {
bool registerFile1Types(LTypeRegistry & registry)
{
    bool ok = true;
    ok = registry.registerTopicType<P1::P2::Handle>(
        "file1::HANDLE_TOPIC",
        "P1::P2::Handle",
        [](const P1::P2::Handle & object, std::vector<uint8_t> & outPayload) -> bool {
            outPayload = object.serialize();
            return true;
        },
        [](const std::vector<uint8_t> & payload, P1::P2::Handle & object) -> bool {
            return object.deserialize(payload);
        }
    ) && ok;
    ok = registry.registerTopicType<P1::Param1>(
        "file1::PARAM1_TOPIC",
        "P1::Param1",
        [](const P1::Param1 & object, std::vector<uint8_t> & outPayload) -> bool {
            outPayload = object.serialize();
            return true;
        },
        [](const std::vector<uint8_t> & payload, P1::Param1 & object) -> bool {
            return object.deserialize(payload);
        }
    ) && ok;
    return ok;
}

extern "C" FILE1_IDL_API bool registerFile1IdlModule(LTypeRegistry & registry)
{
    return registerFile1Types(registry);
}
} // namespace LDdsFramework

namespace {
struct File1AutoModuleRegistrar
{
    File1AutoModuleRegistrar()
    {
        LDdsFramework::registerGeneratedModule(
            "file1",
            &LDdsFramework::registerFile1IdlModule);
    }
};
static File1AutoModuleRegistrar g_file1AutoModuleRegistrar;
} // namespace
