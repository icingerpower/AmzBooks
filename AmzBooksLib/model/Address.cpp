#include "Address.h"
#include <QJsonObject>

Address Address::fromJson(const QJsonObject &json) {
    return Address(
        json["fullName"].toString(),
        json["addressLine1"].toString(),
        json["addressLine2"].toString(),
        json["addressLine3"].toString(),
        json["city"].toString(),
        json["postalCode"].toString(),
        json["countryCode"].toString(),
        json["stateOrRegion"].toString(),
        json["email"].toString(),
        json["phone"].toString(),
        json["companyName"].toString(),
        json["taxId"].toString()
    );
}

QJsonObject Address::toJson() const {
    return QJsonObject{
        {"fullName", m_fullName},
        {"addressLine1", m_addressLine1},
        {"addressLine2", m_addressLine2},
        {"addressLine3", m_addressLine3},
        {"city", m_city},
        {"postalCode", m_postalCode},
        {"countryCode", m_countryCode},
        {"stateOrRegion", m_stateOrRegion},
        {"email", m_email},
        {"phone", m_phone},
        {"companyName", m_companyName},
        {"taxId", m_taxId}
    };
}


Address::Address(const QString &fullName,
                 const QString &addressLine1,
                 const QString &addressLine2,
                 const QString &addressLine3,
                 const QString &city,
                 const QString &postalCode,
                 const QString &countryCode,
                 const QString &stateOrRegion,
                 const QString &email,
                 const QString &phone,
                 const QString &companyName,
                 const QString &taxId)
    : m_fullName(fullName)
    , m_addressLine1(addressLine1)
    , m_addressLine2(addressLine2)
    , m_addressLine3(addressLine3)
    , m_city(city)
    , m_postalCode(postalCode)
    , m_countryCode(countryCode)
    , m_stateOrRegion(stateOrRegion)
    , m_email(email)
    , m_phone(phone)
    , m_companyName(companyName)
    , m_taxId(taxId)
{
}

bool Address::isCompleteCompany() const noexcept
{
    return !m_taxId.isEmpty() && !m_companyName.isEmpty();
}

const QString& Address::getFullName() const noexcept
{
    return m_fullName;
}

const QString& Address::getAddressLine1() const noexcept
{
    return m_addressLine1;
}

const QString& Address::getAddressLine2() const noexcept
{
    return m_addressLine2;
}

const QString& Address::getAddressLine3() const noexcept
{
    return m_addressLine3;
}

const QString& Address::getCity() const noexcept
{
    return m_city;
}

const QString& Address::getPostalCode() const noexcept
{
    return m_postalCode;
}

const QString& Address::getCountryCode() const noexcept
{
    return m_countryCode;
}

const QString& Address::getStateOrRegion() const noexcept
{
    return m_stateOrRegion;
}

const QString& Address::getEmail() const noexcept
{
    return m_email;
}

const QString& Address::getPhone() const noexcept
{
    return m_phone;
}

const QString& Address::getCompanyName() const noexcept
{
    return m_companyName;
}

const QString& Address::getTaxId() const noexcept
{
    return m_taxId;
}
