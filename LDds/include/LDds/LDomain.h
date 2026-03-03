/**
 * @file LDomain.h
 * @brief DDS Domain Management Component
 *
 * Provides DDS domain creation, management, and destruction functionality.
 * Domains are isolation boundaries for DDS entity communication.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace LDdsFramework {

// Forward declarations
class LQos;
class LParticipant;

/**
 * @brief Domain identifier type
 */
using DomainId = uint32_t;

/**
 * @brief Invalid domain ID constant
 */
constexpr DomainId INVALID_DOMAIN_ID = static_cast<DomainId>(-1);

/**
 * @brief Default domain ID constant
 */
constexpr DomainId DEFAULT_DOMAIN_ID = 0;

/**
 * @class LDomain
 * @brief DDS Domain Management Class
 *
 * Manages DDS domain lifecycle and provides interfaces for
 * creating and managing entities within the domain.
 */
class LDomain final
{
public:
    /**
     * @brief Default constructor
     *
     * Creates an uninitialized domain instance.
     * Must call create() before use.
     */
    LDomain() noexcept;

    /**
     * @brief Destructor
     *
     * Automatically destroys the domain and releases resources.
     */
    ~LDomain() noexcept;

    /**
     * @brief Copy constructor is deleted
     */
    LDomain(const LDomain& other) = delete;

    /**
     * @brief Copy assignment is deleted
     */
    LDomain& operator=(const LDomain& other) = delete;

    /**
     * @brief Move constructor
     */
    LDomain(LDomain&& other) noexcept;

    /**
     * @brief Move assignment
     */
    LDomain& operator=(LDomain&& other) noexcept;

    /**
     * @brief Create domain
     *
     * Initializes the domain instance, allocating domain ID and resources.
     *
     * @param[in] domainId Domain identifier, DEFAULT_DOMAIN_ID for default domain
     * @param[in] pQos Domain QoS configuration pointer, nullptr for default QoS
     * @return true Creation succeeded
     * @return false Creation failed, domain exists or insufficient resources
     */
    bool create(
        DomainId domainId,              /* Domain identifier */
        const LQos* pQos               /* QoS configuration pointer */
    );

    /**
     * @brief Destroy domain
     *
     * Releases all resources occupied by the domain.
     * All entities within the domain will be destroyed.
     * Safe to call multiple times.
     */
    void destroy() noexcept;

    /**
     * @brief Check if domain is valid
     * @return true Domain is created and valid
     * @return false Domain is not created or destroyed
     */
    bool isValid() const noexcept;

    /**
     * @brief Get domain ID
     * @return Domain identifier, INVALID_DOMAIN_ID for invalid domain
     */
    DomainId getDomainId() const noexcept;

    /**
     * @brief Get domain name
     * @return Reference to domain name string
     */
    const std::string& getName() const noexcept;

    /**
     * @brief Set domain name
     * @param[in] name New domain name
     */
    void setName(const std::string& name);

    /**
     * @brief Get participant count in domain
     * @return Number of participants in current domain
     */
    size_t getParticipantCount() const noexcept;

private:
    /**
     * @brief Domain ID
     */
    DomainId m_domainId;

    /**
     * @brief Domain name
     */
    std::string m_name;

    /**
     * @brief Participant count
     */
    size_t m_participantCount;

    /**
     * @brief Validity flag
     */
    bool m_valid;
};

} // namespace LDdsFramework
