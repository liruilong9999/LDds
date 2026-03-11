#ifndef LDDSFRAMEWORK_LDDSTYPES_H_
#define LDDSFRAMEWORK_LDDSTYPES_H_

#include <cstdint>
#include <string>
#include <vector>

#include "ITransport.h"
#include "LDds_Global.h"

namespace LDdsFramework {

struct DdsInitOptions
{
    std::string qosFile;
    std::string relyFile;
    int32_t domainId;
    std::string profileName;
    TransportConfig transportConfig;
    std::string sourceApp;
    std::string runId;

    DdsInitOptions() noexcept
        : qosFile()
        , relyFile()
        , domainId(-1)
        , profileName()
        , transportConfig()
        , sourceApp()
        , runId()
    {
    }
};

struct DdsPublishOptions
{
    uint64_t simTimestamp;
    std::string sourceApp;
    std::string runId;

    DdsPublishOptions() noexcept
        : simTimestamp(0)
        , sourceApp()
        , runId()
    {
    }
};

struct DdsSampleMetadata
{
    uint64_t simTimestamp;
    uint64_t publishTimestamp;
    uint64_t sequence;
    std::string sourceApp;
    std::string runId;

    DdsSampleMetadata() noexcept
        : simTimestamp(0)
        , publishTimestamp(0)
        , sequence(0)
        , sourceApp()
        , runId()
    {
    }
};

struct DdsCursor
{
    uint64_t lastSequence;

    DdsCursor() noexcept
        : lastSequence(0)
    {
    }
};

struct DdsTopicInfo
{
    uint32_t topicId;
    std::string topicKey;
    std::string typeName;
    std::string moduleName;
    std::string version;

    DdsTopicInfo() noexcept
        : topicId(0)
        , topicKey()
        , typeName()
        , moduleName()
        , version()
    {
    }
};

struct DdsRecordedSample
{
    DdsTopicInfo topicInfo;
    DdsSampleMetadata metadata;
    std::vector<uint8_t> payload;
};

struct DdsRecordIndexEntry
{
    uint64_t offset;
    uint64_t sequence;
    uint64_t simTimestamp;
    std::string topicKey;

    DdsRecordIndexEntry() noexcept
        : offset(0)
        , sequence(0)
        , simTimestamp(0)
        , topicKey()
    {
    }
};

} // namespace LDdsFramework

#endif // LDDSFRAMEWORK_LDDSTYPES_H_
