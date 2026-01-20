#ifndef ACTIVITY_H
#define ACTIVITY_H

// Activity = one immutable-ish normalized accounting posting line (net + VAT) for a single event and single VAT bucket, with tax provenance (marketplace/self/manual) and VAT-territory context.


#include <QString>
#include <QDateTime>

#include "Amount.h"
#include "TaxJurisdictionLevel.h"
#include "TaxScheme.h"
#include "TaxSource.h"
#include "Result.h"
#include "SaleType.h"

class Activity final
{
public:
    static Result<Activity> create(QString eventId,                 // Order/shipment/refund/stock-move external ID (scoped by the owner entity)
                                   QString activityId,              // Normalized immutable ID for this activity line
                                   QDateTime dateTime,              // Bookkeeping datetime (recognition time)
                                   QString currency,                // ISO 4217 (e.g., "EUR")
                                   QString countryCodeFrom,         // ISO 3166-1 alpha-2
                                   QString countryCodeTo,           // ISO 3166-1 alpha-2
                                   QString countryCodeVatPaidTo,    // ISO 3166-1 alpha-2
                                   Amount amountSource,             // Net + Tax (Source)
                                   TaxSource taxSource,             // Marketplace/Self/Manual/Unknown
                                   QString taxDeclaringCountryCode, // ISO 3166-1 alpha-2 where VAT is declared
                                   TaxScheme taxScheme,             // e.g., EuOssUnion
                                   TaxJurisdictionLevel taxJurisdictionLevel, // usually Country in EU
                                   SaleType saleType,
                                   QString vatTerritoryFrom = QString{},
                                   QString vatTerritoryTo   = QString{});

    bool isDifferentTaxes(const Activity &other) const;

    void setTaxes(double taxes);

    const QString& getEventId() const noexcept;
    const QString& getActivityId() const noexcept;
    const QDateTime& getDateTime() const noexcept;
    const QString& getCurrency() const noexcept;
    const QString& getCountryCodeFrom() const noexcept;
    const QString& getCountryCodeTo() const noexcept;
    const QString& getCountryCodeVatPaidTo() const noexcept;

    double getAmountUntaxed() const noexcept;
    double getAmountTaxed() const noexcept;
    double getAmountTaxes() const noexcept;
    double getAmountTaxesSource() const noexcept;
    double getAmountTaxesComputed() const noexcept;
    TaxSource getTaxSource() const noexcept;
    const QString& getTaxDeclaringCountryCode() const noexcept;
    TaxScheme getTaxScheme() const noexcept;
    TaxJurisdictionLevel getTaxJurisdictionLevel() const noexcept;
    SaleType getSaleType() const noexcept;
    const QString& getVatTerritoryFrom() const noexcept;
    const QString& getVatTerritoryTo() const noexcept;

    QString getVatRate_4digits() const noexcept;
    QString getVatRate_2digits() const noexcept;
    double getVatRate() const noexcept;

private:
    Activity(QString eventId,
             QString activityId,
             QDateTime dateTime,
             QString currency,
             QString countryCodeFrom,
             QString countryCodeTo,
             QString countryCodeVatPaidTo,
             Amount amountSource,
             TaxSource taxSource,
             QString taxDeclaringCountryCode,
             TaxScheme taxScheme,
             TaxJurisdictionLevel taxJurisdictionLevel,
             SaleType saleType,
             QString vatTerritoryFrom,
             QString vatTerritoryTo);

    QString m_eventId;
    QString m_activityId;
    QDateTime m_dateTime;
    QString m_currency;
    QString m_countryCodeFrom;
    QString m_countryCodeTo;
    QString m_countryCodeVatPaidTo;

    Amount m_amountSource;
    double m_AmountTaxesComputed = 0.0;

    TaxSource m_taxSource = TaxSource::Unknown;
    QString m_taxDeclaringCountryCode;

    TaxScheme m_taxScheme = TaxScheme::Unknown;
    TaxJurisdictionLevel m_taxJurisdictionLevel = TaxJurisdictionLevel::Unknown;
    SaleType m_saleType = SaleType::Products;

    QString m_vatTerritoryFrom;
    QString m_vatTerritoryTo;
};



#endif // ACTIVITY_H
