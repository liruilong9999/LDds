#pragma once

#include <string>
#include <unordered_map>

class QoSConfig
{
public:
    bool        loadFromFile(const std::string & path);
    void        setQos(int topic, const std::string & profile);
    std::string getQos(int topic) const;

private:
    std::unordered_map<int, std::string> m_qos;
};
