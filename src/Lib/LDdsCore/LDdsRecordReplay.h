#ifndef LDDSFRAMEWORK_LDDSRECORDREPLAY_H_
#define LDDSFRAMEWORK_LDDSRECORDREPLAY_H_

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "LDdsTypes.h"
#include "LDds_Global.h"

namespace LDdsFramework {

class LDDSCORE_EXPORT DdsRecorder final
{
public:
    DdsRecorder();
    ~DdsRecorder() noexcept;

    bool open(const std::string & filePath, std::string * errorMessage = nullptr);
    void close() noexcept;
    bool isOpen() const noexcept;

    bool record(
        const DdsTopicInfo & topicInfo,
        const DdsSampleMetadata & metadata,
        const std::vector<uint8_t> & payload,
        std::string * errorMessage = nullptr);

private:
    std::ofstream m_stream;
};

class LDDSCORE_EXPORT DdsPlaybackParser final
{
public:
    DdsPlaybackParser();
    ~DdsPlaybackParser() noexcept;

    bool open(const std::string & filePath, std::string * errorMessage = nullptr);
    void close() noexcept;
    bool isOpen() const noexcept;
    bool reset(std::string * errorMessage = nullptr);
    bool readNext(DdsRecordedSample & sample, std::string * errorMessage = nullptr);

private:
    std::ifstream m_stream;
    std::string m_filePath;
};

class LDDSCORE_EXPORT DdsRecordIndexBuilder final
{
public:
    static bool build(
        const std::string & filePath,
        std::vector<DdsRecordIndexEntry> & indexEntries,
        std::string * errorMessage = nullptr);
};

} // namespace LDdsFramework

#endif // LDDSFRAMEWORK_LDDSRECORDREPLAY_H_
