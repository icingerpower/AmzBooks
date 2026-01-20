#include "TaxResolver.h"
#include <QDate>
#include <QDateTime>

TaxResolver::TaxResolver(const QString &workingDir)
    : m_vatTerritoryResolver{workingDir}
{

}

bool TaxResolver::isVatTerritory(
        const QString &countryCode
        , const QString &postalCode
        , const QString &city
        , const QString &countryCodeVatPaidTo) const noexcept
{
    return !m_vatTerritoryResolver.getTerritoryId(countryCode, postalCode, city).isEmpty();

}

bool TaxResolver::isEuMember(const QString &countryCode, const QDate &date)
{
    if (countryCode == "GB") {
        // Brexit: GB is EU member until 2020-12-31 inclusive.
        return date < QDate(2021, 1, 1);
    }

    static const QSet<QString> euCountries = {
        "AT", "BE", "BG", "CY", "CZ", "DE", "DK", "EE", "ES", "FI", "FR", "GR", "HR", "HU", "IE", "IT", "LT", "LU", "LV", "MT", "NL", "PL", "PT", "RO", "SE", "SI", "SK", "MC", "XI"
    };
    return euCountries.contains(countryCode);
}


    TaxResolver::TaxContext TaxResolver::getTaxContext(
        const QDateTime &dateTime,
        const QString &countryCodeFrom,
        const QString &countryCodeTo,
        SaleType saleType,
        bool isToBusiness,
        bool isIoss,
        const QString &vatTerritoryFrom,
        const QString &vatTerritoryTo) const
{
    // 1. Resolve effective origin/destination
    //    If a special territory is involved (e.g., IT-LIVIGNO), it acts like a separate 'country' for VAT purposes, usually non-EU.
    //    Ideally, vatTerritoryFrom/To are sufficient. If empty, fall back to countryCode.
    //    However, our logic below relies on determining EU membership.
    //    Special territories in our VatTerritoryResolver are generally "Excluded from EU VAT area".
    //    So if vatTerritoryFrom is present, we treat it as Non-EU (conceptually).

    QString effectiveOrigin = vatTerritoryFrom.isEmpty() ? countryCodeFrom : vatTerritoryFrom;
    QString effectiveDestination = vatTerritoryTo.isEmpty() ? countryCodeTo : vatTerritoryTo;

    // Check effective territory for EU membership.
    // This allows identifying XI (Northern Ireland) or MC (Monaco) as EU, 
    // while excluding others like Canary Islands (ES-CANARY) which are not in the EU list.
    bool isOriginEu = isEuMember(effectiveOrigin, dateTime.date());
    bool isDestEu = isEuMember(effectiveDestination, dateTime.date());

    TaxContext context;
    context.taxDeclaringCountryCode = "";
    context.taxScheme = TaxScheme::Unknown;
    context.taxJurisdictionLevel = TaxJurisdictionLevel::Unknown;
    context.countryCodeVatPaidTo = ""; // Default empty, update if logic dictates

    // Case 1: Domestic (Same Country)
    // Note: If both are the SAME special territory, it's domestic within that territory.
    // If one is territory and other is mainland same country (e.g. IT -> IT-LIVIGNO), it's Export.
    bool sameCountry = (countryCodeFrom == countryCodeTo);
    bool sameTerritory = (vatTerritoryFrom == vatTerritoryTo); // Handles both empty or both same non-empty

    if (sameCountry && sameTerritory) {
        if (isOriginEu) {
            context.taxScheme = TaxScheme::DomesticVat;
            context.taxDeclaringCountryCode = countryCodeFrom;
            context.countryCodeVatPaidTo = countryCodeFrom; // Domestic VAT paid to home country
            context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
            return context;
        }
    }

    // Case 2: Intra-Community Supply (EU -> EU) (Different countries)
    if (isOriginEu && isDestEu) {
        if (isToBusiness) {
             // B2B: Intra-Community Supply (Exempt in Origin, Reverse Charge in Dest)
            context.taxScheme = TaxScheme::Exempt;
            context.taxDeclaringCountryCode = countryCodeFrom; 
            context.countryCodeVatPaidTo = countryCodeTo; // Reverse Charge: Liability in Dest
            context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
            return context;
        } else {
            // B2C logic: With OSS (Union Scheme), we charge VAT of destination country.
            // OSS Filing is in Origin (Declaring), but Tax is paid to Destination.
            context.taxScheme = TaxScheme::EuOssUnion;
            context.taxDeclaringCountryCode = countryCodeFrom; // OSS return filed in Origin
            context.countryCodeVatPaidTo = countryCodeTo; // VAT belongs to Destination
            context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
            return context;
        }
    }

    // Case 3: Export (EU -> Non-EU)
    if (isOriginEu && !isDestEu) {
        context.taxScheme = TaxScheme::Exempt; // Export is exempt with right to deduct
        context.taxDeclaringCountryCode = countryCodeFrom;
        context.countryCodeVatPaidTo = countryCodeTo; // Export: Liability generally considered in Dest (Import)
        context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
        return context;
    }

    // Case 4: Import (Non-EU -> EU)
    if (!isOriginEu && isDestEu) {
        if (isToBusiness) {
            // Business import usually uses Reverse Charge / Postponed Accounting
            context.taxScheme = TaxScheme::ReverseChargeImport;
             // VAT is accounted for in Destination
            context.taxDeclaringCountryCode = countryCodeTo;
            context.countryCodeVatPaidTo = countryCodeTo;
            context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
            return context;
        } else {
             // IOSS or Standard Import.
             if (isIoss) {
                 context.taxScheme = TaxScheme::EuIoss;
                 context.taxDeclaringCountryCode = countryCodeTo; 
                 context.countryCodeVatPaidTo = countryCodeTo;
                 context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
                 return context;
             }
             // Without IOSS flag input, let's assume standard Import Vat at border.
            context.taxScheme = TaxScheme::ImportVat;
            context.taxDeclaringCountryCode = countryCodeTo; // Import VAT paid in Dest
            context.countryCodeVatPaidTo = countryCodeTo;
            context.taxJurisdictionLevel = TaxJurisdictionLevel::Country;
            return context;
        }
    }

    // Case 5: Outside Scope (Non-EU -> Non-EU)
    if (!isOriginEu && !isDestEu) {
        context.taxScheme = TaxScheme::OutOfScope;
        context.taxDeclaringCountryCode = "";
        context.countryCodeVatPaidTo = "";
        context.taxJurisdictionLevel = TaxJurisdictionLevel::Unknown;
        return context;
    }

    return context;
}


