#ifndef ACTIVITY_H
#define ACTIVITY_H

// Can reprente an order shipment / return / refund and also inventory country re-location

#include <QString>
#include <QDateTime>

#include "TaxJurisdictionLevel.h"
#include "TaxScheme.h"
#include "TaxSource.h"

class Activity final
{
public:
    Activity(QString eventId,                 // Order/shipment/refund/stock-move external ID (scoped by the owner entity)
             QDateTime dateTime,              // Bookkeeping datetime (recognition time)
             QString currency,                // ISO 4217 (e.g., "EUR")
             QString countryCodeFrom,         // ISO 3166-1 alpha-2
             QString countryCodeTo,           // ISO 3166-1 alpha-2
             double amountTaxed,              // Taxable base (net) in currency unit (see note in cpp)
             double amountTaxes,              // VAT amount in currency unit (see note in cpp)
             TaxSource taxSource,             // Marketplace/Self/Manual/Unknown
             QString taxDeclaringCountryCode, // ISO 3166-1 alpha-2 where VAT is declared
             TaxScheme taxScheme,             // e.g., EuOssUnion
             TaxJurisdictionLevel taxJurisdictionLevel, // usually Country in EU
             QString vatTerritoryFrom = QString{},
             QString vatTerritoryTo   = QString{});

    void setTaxes(double taxes);

    const QString& getEventId() const noexcept;
    const QDateTime& getDateTime() const noexcept;
    const QString& getCurrency() const noexcept;
    const QString& getCountryCodeFrom() const noexcept;
    const QString& getCountryCodeTo() const noexcept;
    double getAmountUntaxed() const noexcept;
    double getAmountTaxed() const noexcept;
    double getAmountTaxes() const noexcept;
    double getAmountTaxesSource() const noexcept;
    double getAmountTaxesComputed() const noexcept;
    TaxSource getTaxSource() const noexcept;
    const QString& getTaxDeclaringCountryCode() const noexcept;
    TaxScheme getTaxScheme() const noexcept;
    TaxJurisdictionLevel getTaxJurisdictionLevel() const noexcept;
    const QString& getVatTerritoryFrom() const noexcept;
    const QString& getVatTerritoryTo() const noexcept;

private:
    QString m_eventId;
    QDateTime m_dateTime;
    QString m_currency;
    QString m_countryCodeFrom;
    QString m_countryCodeTo;

    double m_amountTaxed = 0.0;
    double m_AmountTaxesSource = 0.0;
    double m_AmountTaxesComputed = 0.0;

    TaxSource m_taxSource = TaxSource::Unknown;
    QString m_taxDeclaringCountryCode;

    TaxScheme m_taxScheme = TaxScheme::Unknown;
    TaxJurisdictionLevel m_taxJurisdictionLevel = TaxJurisdictionLevel::Unknown;

    QString m_vatTerritoryFrom;
    QString m_vatTerritoryTo;
};



#endif // ACTIVITY_H
