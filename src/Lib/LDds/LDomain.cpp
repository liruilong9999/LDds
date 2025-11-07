#include "LDomain.h"
#include <iostream>

bool LDomain::init(int domainId)
{
    std::cout << "LDomain::init(" << domainId << ")" << std::endl;
    return true;
}

int LDomain::getDomainId() const
{
    return 0;
}
std::string LDomain::getDomainName() const
{
    return "LDdsDomain";
}
std::any LDomain::getDataByTopic(int topic)
{
    return std::any();
}
void * LDomain::createFindSet(int topic)
{
    return nullptr;
}
std::any LDomain::getTopicData(void * findSet)
{
    return std::any();
}
std::any LDomain::getNextTopicData(void * findSet)
{
    return std::any();
}
