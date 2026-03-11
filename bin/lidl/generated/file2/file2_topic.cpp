#include "file2_topic.h"

namespace LDdsFramework {
bool registerFile2Types(LTypeRegistry & registry)
{
    bool ok = true;
    ok = registry.registerTopicType<P3::TestParam>(
        "file2::TESTPARAM_TOPIC",
        "P3::TestParam",
        [](const P3::TestParam & object, std::vector<uint8_t> & outPayload) -> bool {
            outPayload = object.serialize();
            return true;
        },
        [](const std::vector<uint8_t> & payload, P3::TestParam & object) -> bool {
            return object.deserialize(payload);
        }
    ) && ok;
    return ok;
}

extern "C" FILE2_IDL_API bool registerFile2IdlModule(LTypeRegistry & registry)
{
    return registerFile2Types(registry);
}
} // namespace LDdsFramework

namespace {
struct File2AutoModuleRegistrar
{
    File2AutoModuleRegistrar()
    {
        LDdsFramework::registerGeneratedModule(
            "file2",
            &LDdsFramework::registerFile2IdlModule);
    }
};
static File2AutoModuleRegistrar g_file2AutoModuleRegistrar;
} // namespace
