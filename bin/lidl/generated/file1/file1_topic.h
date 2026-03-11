#ifndef FILE1_TOPIC_H
#define FILE1_TOPIC_H

#include <cstdint>
#include <string>

#include "file1_export.h"
#include "file1_define.h"
#include "LTypeRegistry.h"

namespace LDdsFramework {
#define FILE1_TOPIC_NAME_HANDLE_TOPIC "HANDLE_TOPIC"
#define FILE1_TOPIC_KEY_HANDLE_TOPIC "file1::HANDLE_TOPIC"
#define FILE1_TOPIC_ID_HANDLE_TOPIC LDdsFramework::LTypeRegistry::makeTopicId(FILE1_TOPIC_KEY_HANDLE_TOPIC)
#define FILE1_TOPIC_NAME_PARAM1_TOPIC "PARAM1_TOPIC"
#define FILE1_TOPIC_KEY_PARAM1_TOPIC "file1::PARAM1_TOPIC"
#define FILE1_TOPIC_ID_PARAM1_TOPIC LDdsFramework::LTypeRegistry::makeTopicId(FILE1_TOPIC_KEY_PARAM1_TOPIC)

FILE1_IDL_API bool registerFile1Types(LTypeRegistry & registry);
extern "C" FILE1_IDL_API bool registerFile1IdlModule(LTypeRegistry & registry);
inline bool tryResolveFile1TopicId(const std::string & topicKey, uint32_t & topicId)
{
    if (topicKey == "file1::HANDLE_TOPIC" || topicKey == "HANDLE_TOPIC")
    {
        topicId = LTypeRegistry::makeTopicId("file1::HANDLE_TOPIC");
        return true;
    }
    if (topicKey == "file1::PARAM1_TOPIC" || topicKey == "PARAM1_TOPIC")
    {
        topicId = LTypeRegistry::makeTopicId("file1::PARAM1_TOPIC");
        return true;
    }
    topicId = 0;
    return false;
}

inline bool tryResolveFile1TopicKey(uint32_t topicId, const char * & topicKey)
{
    if (topicId == LTypeRegistry::makeTopicId("file1::HANDLE_TOPIC"))
    {
        topicKey = "file1::HANDLE_TOPIC";
        return true;
    }
    if (topicId == LTypeRegistry::makeTopicId("file1::PARAM1_TOPIC"))
    {
        topicKey = "file1::PARAM1_TOPIC";
        return true;
    }
    topicKey = nullptr;
    return false;
}
} // namespace LDdsFramework

#endif // FILE1_TOPIC_H
