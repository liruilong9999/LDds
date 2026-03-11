#include "LDdsRecordReplay.h"

#include <algorithm>

namespace LDdsFramework {
namespace {

constexpr uint32_t RECORD_MAGIC = 0x4C445253U; // LDRS
constexpr uint32_t RECORD_VERSION = 1U;

void writeU16(std::ostream & stream, uint16_t value)
{
    char data[2];
    data[0] = static_cast<char>(value & 0xFFU);
    data[1] = static_cast<char>((value >> 8) & 0xFFU);
    stream.write(data, sizeof(data));
}

void writeU32(std::ostream & stream, uint32_t value)
{
    char data[4];
    data[0] = static_cast<char>(value & 0xFFU);
    data[1] = static_cast<char>((value >> 8) & 0xFFU);
    data[2] = static_cast<char>((value >> 16) & 0xFFU);
    data[3] = static_cast<char>((value >> 24) & 0xFFU);
    stream.write(data, sizeof(data));
}

void writeU64(std::ostream & stream, uint64_t value)
{
    char data[8];
    for (int i = 0; i < 8; ++i)
    {
        data[i] = static_cast<char>((value >> (8 * i)) & 0xFFU);
    }
    stream.write(data, sizeof(data));
}

bool readBytes(std::istream & stream, void * data, std::size_t size)
{
    stream.read(static_cast<char *>(data), static_cast<std::streamsize>(size));
    return stream.good();
}

bool readU16(std::istream & stream, uint16_t & value)
{
    unsigned char data[2] = {0U, 0U};
    if (!readBytes(stream, data, sizeof(data)))
    {
        return false;
    }
    value = static_cast<uint16_t>(data[0]) |
            static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
    return true;
}

bool readU32(std::istream & stream, uint32_t & value)
{
    unsigned char data[4] = {0U, 0U, 0U, 0U};
    if (!readBytes(stream, data, sizeof(data)))
    {
        return false;
    }
    value = static_cast<uint32_t>(data[0]) |
            (static_cast<uint32_t>(data[1]) << 8) |
            (static_cast<uint32_t>(data[2]) << 16) |
            (static_cast<uint32_t>(data[3]) << 24);
    return true;
}

bool readU64(std::istream & stream, uint64_t & value)
{
    unsigned char data[8] = {0U};
    if (!readBytes(stream, data, sizeof(data)))
    {
        return false;
    }

    value = 0;
    for (int i = 0; i < 8; ++i)
    {
        value |= (static_cast<uint64_t>(data[i]) << (8 * i));
    }
    return true;
}

void writeString(std::ostream & stream, const std::string & value)
{
    const std::size_t textSize = std::min<std::size_t>(value.size(), 65535U);
    writeU16(stream, static_cast<uint16_t>(textSize));
    if (textSize > 0)
    {
        stream.write(value.data(), static_cast<std::streamsize>(textSize));
    }
}

bool readString(std::istream & stream, std::string & value)
{
    uint16_t textSize = 0;
    if (!readU16(stream, textSize))
    {
        return false;
    }

    value.resize(textSize);
    if (textSize == 0)
    {
        return true;
    }

    return readBytes(stream, &value[0], textSize);
}

bool ensureHeader(std::istream & stream)
{
    uint32_t magic = 0;
    uint32_t version = 0;
    return readU32(stream, magic) &&
           readU32(stream, version) &&
           magic == RECORD_MAGIC &&
           version == RECORD_VERSION;
}

void writeHeader(std::ostream & stream)
{
    writeU32(stream, RECORD_MAGIC);
    writeU32(stream, RECORD_VERSION);
}

bool readSample(std::istream & stream, DdsRecordedSample & sample)
{
    uint32_t payloadSize = 0;
    sample = DdsRecordedSample();
    if (!readString(stream, sample.topicInfo.topicKey) ||
        !readString(stream, sample.topicInfo.typeName) ||
        !readString(stream, sample.topicInfo.moduleName) ||
        !readString(stream, sample.topicInfo.version) ||
        !readU32(stream, sample.topicInfo.topicId) ||
        !readU64(stream, sample.metadata.simTimestamp) ||
        !readU64(stream, sample.metadata.publishTimestamp) ||
        !readU64(stream, sample.metadata.sequence) ||
        !readString(stream, sample.metadata.sourceApp) ||
        !readString(stream, sample.metadata.runId) ||
        !readU32(stream, payloadSize))
    {
        return false;
    }

    sample.payload.resize(payloadSize);
    if (payloadSize == 0)
    {
        return true;
    }

    return readBytes(stream, sample.payload.data(), payloadSize);
}

} // namespace

DdsRecorder::DdsRecorder()
    : m_stream()
{
}

DdsRecorder::~DdsRecorder() noexcept
{
    close();
}

bool DdsRecorder::open(const std::string & filePath, std::string * errorMessage)
{
    close();
    m_stream.open(filePath, std::ios::binary | std::ios::trunc);
    if (!m_stream.is_open())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "failed to open record file for write";
        }
        return false;
    }

    writeHeader(m_stream);
    if (!m_stream.good())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "failed to write record header";
        }
        close();
        return false;
    }
    return true;
}

void DdsRecorder::close() noexcept
{
    if (m_stream.is_open())
    {
        m_stream.close();
    }
}

bool DdsRecorder::isOpen() const noexcept
{
    return m_stream.is_open();
}

bool DdsRecorder::record(
    const DdsTopicInfo & topicInfo,
    const DdsSampleMetadata & metadata,
    const std::vector<uint8_t> & payload,
    std::string * errorMessage)
{
    if (!m_stream.is_open())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "record file is not open";
        }
        return false;
    }

    writeString(m_stream, topicInfo.topicKey);
    writeString(m_stream, topicInfo.typeName);
    writeString(m_stream, topicInfo.moduleName);
    writeString(m_stream, topicInfo.version);
    writeU32(m_stream, topicInfo.topicId);
    writeU64(m_stream, metadata.simTimestamp);
    writeU64(m_stream, metadata.publishTimestamp);
    writeU64(m_stream, metadata.sequence);
    writeString(m_stream, metadata.sourceApp);
    writeString(m_stream, metadata.runId);
    writeU32(m_stream, static_cast<uint32_t>(payload.size()));
    if (!payload.empty())
    {
        m_stream.write(
            reinterpret_cast<const char *>(payload.data()),
            static_cast<std::streamsize>(payload.size()));
    }

    if (!m_stream.good())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "failed to write record sample";
        }
        return false;
    }

    return true;
}

DdsPlaybackParser::DdsPlaybackParser()
    : m_stream()
    , m_filePath()
{
}

DdsPlaybackParser::~DdsPlaybackParser() noexcept
{
    close();
}

bool DdsPlaybackParser::open(const std::string & filePath, std::string * errorMessage)
{
    close();
    m_filePath = filePath;
    m_stream.open(filePath, std::ios::binary);
    if (!m_stream.is_open())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "failed to open record file for read";
        }
        return false;
    }

    if (!ensureHeader(m_stream))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "invalid record header";
        }
        close();
        return false;
    }

    return true;
}

void DdsPlaybackParser::close() noexcept
{
    if (m_stream.is_open())
    {
        m_stream.close();
    }
    m_filePath.clear();
}

bool DdsPlaybackParser::isOpen() const noexcept
{
    return m_stream.is_open();
}

bool DdsPlaybackParser::reset(std::string * errorMessage)
{
    if (m_filePath.empty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "record file path is empty";
        }
        return false;
    }

    return open(m_filePath, errorMessage);
}

bool DdsPlaybackParser::readNext(DdsRecordedSample & sample, std::string * errorMessage)
{
    if (!m_stream.is_open())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "record file is not open";
        }
        return false;
    }

    if (m_stream.peek() == std::ifstream::traits_type::eof())
    {
        return false;
    }

    if (!readSample(m_stream, sample))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "failed to parse record sample";
        }
        return false;
    }
    return true;
}

bool DdsRecordIndexBuilder::build(
    const std::string & filePath,
    std::vector<DdsRecordIndexEntry> & indexEntries,
    std::string * errorMessage)
{
    indexEntries.clear();

    std::ifstream stream(filePath, std::ios::binary);
    if (!stream.is_open())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "failed to open record file";
        }
        return false;
    }

    if (!ensureHeader(stream))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "invalid record header";
        }
        return false;
    }

    while (stream.peek() != std::ifstream::traits_type::eof())
    {
        const uint64_t offset = static_cast<uint64_t>(stream.tellg());
        DdsRecordedSample sample;
        if (!readSample(stream, sample))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "failed to build record index";
            }
            return false;
        }

        DdsRecordIndexEntry entry;
        entry.offset = offset;
        entry.sequence = sample.metadata.sequence;
        entry.simTimestamp = sample.metadata.simTimestamp;
        entry.topicKey = sample.topicInfo.topicKey;
        indexEntries.push_back(std::move(entry));
    }

    return true;
}

} // namespace LDdsFramework
