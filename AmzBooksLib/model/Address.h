#ifndef ADDRESS_H
#define ADDRESS_H

#include <QString>

class Address final
{
public:
    Address(const QString &fullName,
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
            const QString &taxId);

    bool isCompleteCompany() const noexcept;
    const QString& getFullName() const noexcept;
    const QString& getAddressLine1() const noexcept;
    const QString& getAddressLine2() const noexcept;
    const QString& getAddressLine3() const noexcept;
    const QString& getCity() const noexcept;
    const QString& getPostalCode() const noexcept;
    const QString& getCountryCode() const noexcept;
    const QString& getStateOrRegion() const noexcept;
    const QString& getEmail() const noexcept;
    const QString& getPhone() const noexcept;
    const QString& getCompanyName() const noexcept;
    const QString& getTaxId() const noexcept;

private:
    QString m_fullName;
    QString m_addressLine1;
    QString m_addressLine2;
    QString m_addressLine3;
    QString m_city;
    QString m_postalCode;
    QString m_countryCode;
    QString m_stateOrRegion;
    QString m_email;
    QString m_phone;
    QString m_companyName;
    QString m_taxId;
};

#endif // ADDRESS_H
