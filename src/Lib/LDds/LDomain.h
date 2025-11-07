#pragma once

#include <string>
#include <memory>
#include <any>

class LDomain
{
public:
    LDomain()  = default;
    ~LDomain() = default;

    bool        init(int domainId);
    int         getDomainId() const;
    std::string getDomainName() const;

    // Topic data access
    std::any getDataByTopic(int topic);
    // Find set API (returns an opaque handle)
    void *   createFindSet(int topic);
    std::any getTopicData(void * findSet);
    std::any getNextTopicData(void * findSet);
};
