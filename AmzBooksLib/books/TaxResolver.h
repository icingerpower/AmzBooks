#ifndef TAXRESOLVER_H
#define TAXRESOLVER_H

#include <QString>
#include <QDir>

#include "TaxScheme.h"
#include "TaxJurisdictionLevel.h"
#include "orders/SaleType.h"
#include "VatTerritoryResolver.h"

// TaxResolver = deterministic VAT decision engine: given a tax context (from/to locations, buyer B2B/B2C, marketplace role, supply kind),
// it resolves special VAT territories (via IVatTerritoryRepository) and outputs a TaxDetermination (tax scheme, declaring country, jurisdiction level),
// plus confidence/missing-fields for incomplete data; it performs no I/O and has no side effects.


class TaxResolver
{
public:
    TaxResolver(const QDir &workingDir);
    struct TaxContext{
        QString taxDeclaringCountryCode;
        TaxScheme taxScheme;
        TaxJurisdictionLevel taxJurisdictionLevel;
        QString countryCodeVatPaidTo;

        bool operator==(const TaxContext &other) const {
            return taxDeclaringCountryCode == other.taxDeclaringCountryCode &&
                   taxScheme == other.taxScheme &&
                   taxJurisdictionLevel == other.taxJurisdictionLevel &&
                   countryCodeVatPaidTo == other.countryCodeVatPaidTo;
        }
    };


    bool isVatTerritory(
            const QString &countryCode
            , const QString &postalCode
            , const QString &city
            , const QString &countryCodeVatPaidTo = QString()) const noexcept; // Only return a value if the city is not as the countryCode for taxes
    TaxContext getTaxContext(const QDateTime &dateTime,
                             const QString &countryCodeFrom,
                             const QString &countryCodeTo,
                             SaleType saleType,
                             bool isToBusiness,
                             bool isIoss = false,
                             const QString &vatTerritoryFrom = QString(), // Empty means no territory
                             const QString &vatTerritoryTo = QString()) const;


private:
    VatTerritoryResolver m_vatTerritoryResolver;
};

inline uint qHash(const TaxResolver::TaxContext &key, uint seed = 0) {
    return qHash(key.taxDeclaringCountryCode, seed) ^
           qHash(static_cast<int>(key.taxScheme), seed) ^
           qHash(static_cast<int>(key.taxJurisdictionLevel), seed) ^
           qHash(key.countryCodeVatPaidTo, seed);
}

#endif // VATRESOLVER_H
