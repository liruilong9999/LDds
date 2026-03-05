#ifndef LQT_COMPAT_H
#define LQT_COMPAT_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ostream>
#include <string>

using qint8 = std::int8_t;
using quint8 = std::uint8_t;
using qint16 = std::int16_t;
using quint16 = std::uint16_t;
using qint32 = std::int32_t;
using quint32 = std::uint32_t;
using qint64 = std::int64_t;
using quint64 = std::uint64_t;
using qulonglong = unsigned long long;

class LString
{
public:
    LString() = default;

    LString(const char* text)
        : value_(text == nullptr ? "" : text)
    {
    }

    LString(const std::string& text)
        : value_(text)
    {
    }

    static LString fromStdString(const std::string& text)
    {
        return LString(text);
    }

    std::string toStdString() const
    {
        return value_;
    }

    const char* c_str() const noexcept
    {
        return value_.c_str();
    }

    bool isEmpty() const noexcept
    {
        return value_.empty();
    }

    void clear() noexcept
    {
        value_.clear();
    }

    LString trimmed() const
    {
        size_t begin = 0;
        while (begin < value_.size() && std::isspace(static_cast<unsigned char>(value_[begin])) != 0)
        {
            ++begin;
        }

        size_t end = value_.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(value_[end - 1])) != 0)
        {
            --end;
        }

        return LString(value_.substr(begin, end - begin));
    }

    LString arg(quint32 value) const
    {
        return replaceFirstPlaceholder(LString(std::to_string(value)));
    }

    LString arg(quint16 value) const
    {
        return replaceFirstPlaceholder(LString(std::to_string(static_cast<quint32>(value))));
    }

    LString arg(int value) const
    {
        return replaceFirstPlaceholder(LString(std::to_string(value)));
    }

    LString arg(const LString& value) const
    {
        return replaceFirstPlaceholder(value);
    }

    LString arg(const LString& first, const LString& second) const
    {
        return replaceFirstPlaceholder(first).replaceFirstPlaceholder(second);
    }

    LString& operator=(const char* text)
    {
        value_ = (text == nullptr) ? "" : text;
        return *this;
    }

    LString& operator=(const std::string& text)
    {
        value_ = text;
        return *this;
    }

    LString& operator+=(const LString& other)
    {
        value_ += other.value_;
        return *this;
    }

    friend LString operator+(const LString& lhs, const LString& rhs)
    {
        return LString(lhs.value_ + rhs.value_);
    }

    friend bool operator==(const LString& lhs, const LString& rhs) noexcept
    {
        return lhs.value_ == rhs.value_;
    }

    friend bool operator!=(const LString& lhs, const LString& rhs) noexcept
    {
        return !(lhs == rhs);
    }

    friend bool operator<(const LString& lhs, const LString& rhs) noexcept
    {
        return lhs.value_ < rhs.value_;
    }

    friend std::ostream& operator<<(std::ostream& os, const LString& value)
    {
        os << value.value_;
        return os;
    }

private:
    LString replaceFirstPlaceholder(const LString& replacement) const
    {
        const size_t markerPos = value_.find('%');
        if (markerPos == std::string::npos || markerPos + 1 >= value_.size())
        {
            return *this;
        }

        if (!std::isdigit(static_cast<unsigned char>(value_[markerPos + 1])))
        {
            return *this;
        }

        std::string result = value_;
        result.replace(markerPos, 2, replacement.value_);
        return LString(result);
    }

private:
    std::string value_;
};

class LHostAddress
{
public:
    enum SpecialAddress
    {
        Null = 0,
        Broadcast,
        AnyIPv4
    };

    LHostAddress() noexcept
        : valid_(false)
        , ipv4_(0)
    {
    }

    LHostAddress(SpecialAddress special) noexcept
        : valid_(false)
        , ipv4_(0)
    {
        setSpecial(special);
    }

    LHostAddress(const LString& text) noexcept
        : valid_(false)
        , ipv4_(0)
    {
        setAddress(text);
    }

    LHostAddress(const std::string& text) noexcept
        : valid_(false)
        , ipv4_(0)
    {
        setAddress(LString::fromStdString(text));
    }

    LHostAddress(const char* text) noexcept
        : valid_(false)
        , ipv4_(0)
    {
        setAddress(LString(text));
    }

    bool setAddress(const LString& text) noexcept
    {
        std::uint32_t parsed = 0;
        if (!parseIpv4(text.toStdString(), parsed))
        {
            clear();
            return false;
        }

        valid_ = true;
        ipv4_ = parsed;
        return true;
    }

    bool isNull() const noexcept
    {
        return !valid_;
    }

    bool isMulticast() const noexcept
    {
        if (!valid_)
        {
            return false;
        }

        const std::uint8_t first = static_cast<std::uint8_t>((ipv4_ >> 24) & 0xFFU);
        return first >= 224U && first <= 239U;
    }

    LString toString() const
    {
        if (!valid_)
        {
            return LString();
        }
        return LString(ipv4ToString(ipv4_));
    }

    void clear() noexcept
    {
        valid_ = false;
        ipv4_ = 0;
    }

    std::uint32_t toIPv4Address() const noexcept
    {
        return valid_ ? ipv4_ : 0U;
    }

    static LHostAddress fromIPv4Address(std::uint32_t value) noexcept
    {
        LHostAddress address;
        address.valid_ = true;
        address.ipv4_ = value;
        return address;
    }

    friend bool operator==(const LHostAddress& lhs, const LHostAddress& rhs) noexcept
    {
        if (lhs.valid_ != rhs.valid_)
        {
            return false;
        }
        if (!lhs.valid_)
        {
            return true;
        }
        return lhs.ipv4_ == rhs.ipv4_;
    }

    friend bool operator!=(const LHostAddress& lhs, const LHostAddress& rhs) noexcept
    {
        return !(lhs == rhs);
    }

private:
    void setSpecial(SpecialAddress special) noexcept
    {
        switch (special)
        {
        case Broadcast:
            valid_ = true;
            ipv4_ = 0xFFFFFFFFU;
            break;
        case AnyIPv4:
            valid_ = true;
            ipv4_ = 0U;
            break;
        case Null:
        default:
            clear();
            break;
        }
    }

    static bool parseIpv4(const std::string& text, std::uint32_t& outValue) noexcept
    {
        std::string value = text;
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
            return std::isspace(ch) == 0;
        }));
        value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
            return std::isspace(ch) == 0;
        }).base(), value.end());

        if (value.empty())
        {
            return false;
        }

        std::uint32_t parts[4] = {0U, 0U, 0U, 0U};
        size_t partIndex = 0;
        size_t cursor = 0;

        while (partIndex < 4U)
        {
            if (cursor >= value.size())
            {
                return false;
            }

            size_t nextDot = value.find('.', cursor);
            const std::string token = value.substr(cursor, nextDot == std::string::npos
                                                               ? std::string::npos
                                                               : nextDot - cursor);
            if (token.empty() || token.size() > 3)
            {
                return false;
            }

            std::uint32_t segment = 0;
            for (const char ch : token)
            {
                if (!std::isdigit(static_cast<unsigned char>(ch)))
                {
                    return false;
                }
                segment = static_cast<std::uint32_t>(segment * 10U + static_cast<std::uint32_t>(ch - '0'));
                if (segment > 255U)
                {
                    return false;
                }
            }

            parts[partIndex++] = segment;
            if (nextDot == std::string::npos)
            {
                break;
            }
            cursor = nextDot + 1;
        }

        if (partIndex != 4U)
        {
            return false;
        }

        outValue = (parts[0] << 24) |
                   (parts[1] << 16) |
                   (parts[2] << 8) |
                   parts[3];
        return true;
    }

    static std::string ipv4ToString(std::uint32_t value)
    {
        return std::to_string((value >> 24) & 0xFFU) + "." +
               std::to_string((value >> 16) & 0xFFU) + "." +
               std::to_string((value >> 8) & 0xFFU) + "." +
               std::to_string(value & 0xFFU);
    }

private:
    bool valid_;
    std::uint32_t ipv4_;
};

#ifndef LStringLiteral
#define LStringLiteral(text_literal) LString(text_literal)
#endif

#ifndef L_UNUSED
#define L_UNUSED(variable) (void)(variable);
#endif

#endif // LQT_COMPAT_H
