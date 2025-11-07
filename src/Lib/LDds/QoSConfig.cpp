#include "QoSConfig.h"
#include <fstream>

bool QoSConfig::loadFromFile(const std::string & path)
{
    // stub: parse simple key=value pairs: topic=profile
    std::ifstream ifs(path);
    if (!ifs)
        return false;
    std::string line;
    while (std::getline(ifs, line))
    {
        auto pos = line.find('=');
        if (pos == std::string::npos)
            continue;
        int         topic   = std::stoi(line.substr(0, pos));
        std::string profile = line.substr(pos + 1);
        m_qos[topic]        = profile;
    }
    return true;
}

void QoSConfig::setQos(int topic, const std::string & profile)
{
    m_qos[topic] = profile;
}

std::string QoSConfig::getQos(int topic) const
{
    auto it = m_qos.find(topic);
    if (it == m_qos.end())
        return std::string();
    return it->second;
}
