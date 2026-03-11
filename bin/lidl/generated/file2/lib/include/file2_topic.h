#ifndef FILE2_TOPIC_H
#define FILE2_TOPIC_H

#include <cstdint>
#include <string>

#include "file2_export.h"
#include "file2_define.h"
#include "LTypeRegistry.h"

namespace LDdsFramework {
#define FILE2_TOPIC_NAME_TESTPARAM_TOPIC "TESTPARAM_TOPIC"
#define FILE2_TOPIC_KEY_TESTPARAM_TOPIC "file2::TESTPARAM_TOPIC"
#define FILE2_TOPIC_ID_TESTPARAM_TOPIC LDdsFramework::LTypeRegistry::makeTopicId(FILE2_TOPIC_KEY_TESTPARAM_TOPIC)

FILE2_IDL_API bool registerFile2Types(LTypeRegistry & registry);
extern "C" FILE2_IDL_API bool registerFile2IdlModule(LTypeRegistry & registry);
inline bool tryResolveFile2TopicId(const std::string & topicKey, uint32_t & topicId)
{
    if (topicKey == "file2::TESTPARAM_TOPIC" || topicKey == "TESTPARAM_TOPIC")
    {
        topicId = LTypeRegistry::makeTopicId("file2::TESTPARAM_TOPIC");
        return true;
    }
    topicId = 0;
    return false;
}

inline bool tryResolveFile2TopicKey(uint32_t topicId, const char * & topicKey)
{
    if (topicId == LTypeRegistry::makeTopicId("file2::TESTPARAM_TOPIC"))
    {
        topicKey = "file2::TESTPARAM_TOPIC";
        return true;
    }
    topicKey = nullptr;
    return false;
}
} // namespace LDdsFramework

#endif // FILE2_TOPIC_H
