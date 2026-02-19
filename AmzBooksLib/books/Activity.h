#ifndef ACTIVITY_H
#define ACTIVITY_H

// Activity = one immutable-ish normalized accounting posting line (net + VAT) for a single event and single VAT bucket, with tax provenance (marketplace/self/manual) and VAT-territory context.


#include <QString>
#include <QDateTime>

#include <QJsonObject>
#include "orders/Amount.h"

#include "TaxJurisdictionLevel.h"
#include "TaxScheme.h"
#include "orders/TaxSource.h"
#include "orders/Result.h"
#include "orders/SaleType.h"

class TaxResolver;
class VatResolver;

class Activity final
{
public:
    static Result<Activity> create(QString eventId,                 // Order/stock-move external ID (scoped by the owner entity)
                                   QString activityId,              // Normalized immutable ID for this activity line, one per shipment/refund
                                   QString subActivityId,           // Optional sub-ID (e.g. for split lines)
                                   QDateTime dateTime,              // Bookkeeping datetime (recognition time)
                                   QDateTime dateTimeTax,           // Tax calculation datetime (e.g. for VAT rate)
                                   QString currency,                // ISO 4217 (e.g., "EUR")
                                   QString countryCodeFrom,         // ISO 3166-1 alpha-2
                                   QString countryCodeTo,           // ISO 3166-1 alpha-2
                                   bool isCompany,                  // true = B2B (business buyer), false = B2C (consumer)
                                   QString countryCodeVatPaidTo,    // ISO 3166-1 alpha-2
                                   Amount amountSource,             // Net + Tax (Source)
                                   TaxSource taxSource,             // Marketplace/Self/Manual/Unknown
                                   QString taxDeclaringCountryCode, // ISO 3166-1 alpha-2 where VAT is declared
                                   TaxScheme taxScheme,             // e.g., EuOssUnion
                                   TaxJurisdictionLevel taxJurisdictionLevel, // usually Country in EU
                                   SaleType saleType,
                                   QString vatTerritoryFrom = QString{},
                                   QString vatTerritoryTo   = QString{},
                                   QString invoiceId        = QString{});

    void setTaxDate(const QDateTime &taxDate);
    void computeTax(const TaxResolver *taxResolver
                    , const VatResolver *vatResolver
                    , const QString &vatTerritoryFrom
                    , const QString &vatTerritoryTo);
    static Activity fromJson(const QJsonObject &json);
    QJsonObject toJson() const;

    bool isDifferentTaxes(const Activity &other) const;

    void setTaxes(double taxes);

    const QString& getEventId() const noexcept;
    const QString& getActivityId() const noexcept;
    const QString& getSubActivityId() const noexcept;
    const QDateTime& getDateTime() const noexcept;
    const QDateTime& getDateTimeTax() const noexcept;
    const QString& getCurrency() const noexcept;
    const QString& getCountryCodeFrom() const noexcept;
    const QString& getCountryCodeTo() const noexcept;
    bool getIsCompany() const noexcept;
    const QString& getCountryCodeVatPaidTo() const noexcept;

    double getAmountUntaxed() const noexcept;
    double getAmountTaxed() const noexcept;
    double getAmountTaxes() const noexcept;
    double getAmountTaxesSource() const noexcept;
    double getAmountTaxesComputed() const noexcept;
    const QString& getInvoiceId() const noexcept;
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
             QString subActivityId,
             QDateTime dateTime,
             QDateTime dateTimeTax,
             QString currency,
             QString countryCodeFrom,
             QString countryCodeTo,
             bool isCompany,
             QString countryCodeVatPaidTo,
             Amount amountSource,
             TaxSource taxSource,
             QString taxDeclaringCountryCode,
             TaxScheme taxScheme,
             TaxJurisdictionLevel taxJurisdictionLevel,
             SaleType saleType,
             QString vatTerritoryFrom,
             QString vatTerritoryTo,
             double taxesComputed,
             QString invoiceId);

    QString m_eventId;
    QString m_activityId;
    QString m_subActivityId;
    QDateTime m_dateTime;
    QDateTime m_dateTimeTax;
    QString m_currency;
    QString m_countryCodeFrom;
    QString m_countryCodeTo;
    bool m_isCompany = false;
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
    QString m_invoiceId;
};



#endif // ACTIVITY_H
