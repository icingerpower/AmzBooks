#include "Activity.h"

Result<Activity> Activity::create(QString eventId,
                                  QString activityId,
                                  QString subActivityId,
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
                                  QString vatTerritoryTo)
{
    Result<Activity> result;

    if (eventId.isEmpty()) {
        result.errors.append(ValidationError{"eventId", "Event ID must not be empty"});
    }
    if (activityId.isEmpty()) {
        result.errors.append(ValidationError{"activityId", "Activity ID must not be empty"});
    }
    if (!dateTime.isValid()) {
        result.errors.append(ValidationError{"dateTime", "DateTime must be valid"});
    }
    if (currency.isEmpty()) {
        result.errors.append(ValidationError{"currency", "Currency must not be empty"});
    }
    if (countryCodeFrom.isEmpty()) {
        result.errors.append(ValidationError{"countryCodeFrom", "Country Code From must not be empty"});
    }
    if (countryCodeTo.isEmpty()) {
        result.errors.append(ValidationError{"countryCodeTo", "Country Code To must not be empty"});
    }
    // ... (rest of validations same)

    if (result.errors.isEmpty()) {
        result.value.emplace(Activity(std::move(eventId),
                                      std::move(activityId),
                                      std::move(subActivityId),
                                      std::move(dateTime),
                                      std::move(currency),
                                      std::move(countryCodeFrom),
                                      std::move(countryCodeTo),
                                      std::move(countryCodeVatPaidTo),
                                      std::move(amountSource),
                                      taxSource,
                                      std::move(taxDeclaringCountryCode),
                                      taxScheme,
                                      taxJurisdictionLevel,
                                      saleType,
                                      std::move(vatTerritoryFrom),
                                      std::move(vatTerritoryTo),
                                      0.0));
    }

    return result;
}

bool Activity::isDifferentTaxes(const Activity &other) const
{
    return getAmountTaxes() != other.getAmountTaxes()
            || m_currency != other.m_currency
            || m_dateTime.date() != other.m_dateTime.date()
            || m_countryCodeFrom != other.m_countryCodeFrom
            || m_countryCodeTo != other.m_countryCodeTo
            || m_countryCodeVatPaidTo != other.m_countryCodeVatPaidTo
            || m_taxDeclaringCountryCode != other.m_taxDeclaringCountryCode
            || m_taxScheme != other.m_taxScheme
            || m_saleType != other.m_saleType
            || m_vatTerritoryFrom != other.m_vatTerritoryFrom
            || m_vatTerritoryTo != other.m_vatTerritoryTo;
}

Activity::Activity(QString eventId,
                   QString activityId,
                   QString subActivityId,
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
                   QString vatTerritoryTo,
                   double taxesComputed)
    : m_eventId(std::move(eventId))
    , m_activityId(std::move(activityId))
    , m_subActivityId(std::move(subActivityId))
    , m_dateTime(std::move(dateTime))
    , m_currency(std::move(currency))
    , m_countryCodeFrom(std::move(countryCodeFrom))
    , m_countryCodeTo(std::move(countryCodeTo))
    , m_countryCodeVatPaidTo(std::move(countryCodeVatPaidTo))
    , m_amountSource(std::move(amountSource))
    , m_taxSource(taxSource)
    , m_taxDeclaringCountryCode(std::move(taxDeclaringCountryCode))
    , m_taxScheme(taxScheme)
    , m_taxJurisdictionLevel(taxJurisdictionLevel)
    , m_saleType(saleType)
    , m_vatTerritoryFrom(std::move(vatTerritoryFrom))
    , m_vatTerritoryTo(std::move(vatTerritoryTo))
    , m_AmountTaxesComputed(taxesComputed)
{
}

void Activity::setTaxes(double taxes)
{
    if (m_taxSource == TaxSource::MarketplaceProvided)
    {
        m_taxSource = TaxSource::ManualOverride;
    }
    else
    {
        m_taxSource = TaxSource::SelfComputed;
    }

    m_AmountTaxesComputed = taxes;
}

const QString& Activity::getEventId() const noexcept
{
    return m_eventId;
}

const QString& Activity::getActivityId() const noexcept
{
    return m_activityId;
}

const QString& Activity::getSubActivityId() const noexcept
{
    return m_subActivityId;
}

const QDateTime& Activity::getDateTime() const noexcept
{
    return m_dateTime;
}

const QString& Activity::getCurrency() const noexcept
{
    return m_currency;
}

const QString& Activity::getCountryCodeFrom() const noexcept
{
    return m_countryCodeFrom;
}

const QString& Activity::getCountryCodeTo() const noexcept
{
    return m_countryCodeTo;
}

const QString& Activity::getCountryCodeVatPaidTo() const noexcept
{
    return m_countryCodeVatPaidTo;
}

double Activity::getAmountUntaxed() const noexcept
{
    return getAmountTaxed() - getAmountTaxes();
}

double Activity::getAmountTaxed() const noexcept
{
    return m_amountSource.getAmountTaxed();
}

double Activity::getAmountTaxes() const noexcept
{
    if (m_taxSource == TaxSource::ManualOverride
            || m_taxSource == TaxSource::SelfComputed)
    {
        return m_AmountTaxesComputed;
    }

    return m_amountSource.getTaxes();
}

double Activity::getAmountTaxesSource() const noexcept
{
    return m_amountSource.getTaxes();
}

double Activity::getAmountTaxesComputed() const noexcept
{
    return m_AmountTaxesComputed;
}

TaxSource Activity::getTaxSource() const noexcept
{
    return m_taxSource;
}

const QString& Activity::getTaxDeclaringCountryCode() const noexcept
{
    return m_taxDeclaringCountryCode;
}

TaxScheme Activity::getTaxScheme() const noexcept
{
    return m_taxScheme;
}

TaxJurisdictionLevel Activity::getTaxJurisdictionLevel() const noexcept
{
    return m_taxJurisdictionLevel;
}

SaleType Activity::getSaleType() const noexcept
{
    return m_saleType;
}

const QString& Activity::getVatTerritoryFrom() const noexcept
{
    return m_vatTerritoryFrom;
}

const QString& Activity::getVatTerritoryTo() const noexcept
{
    return m_vatTerritoryTo;
}

double Activity::getVatRate() const noexcept
{
    double net = getAmountTaxed();
    if (qFuzzyIsNull(net)) {
        return 0.0;
    }
    return getAmountTaxes() / net;
}

QString Activity::getVatRate_4digits() const noexcept
{
    // Return e.g. "0.2000" for 20%
    return QString::number(getVatRate(), 'f', 4);
}

QString Activity::getVatRate_2digits() const noexcept
{
    // Return e.g. "0.20" for 20%
    return QString::number(getVatRate(), 'f', 2);
}

QJsonObject Activity::toJson() const
{
    return QJsonObject{
        {"eventId", m_eventId},
        {"activityId", m_activityId},
        {"subActivityId", m_subActivityId},
        {"dateTime", m_dateTime.toString(Qt::ISODate)},
        {"currency", m_currency},
        {"countryCodeFrom", m_countryCodeFrom},
        {"countryCodeTo", m_countryCodeTo},
        {"countryCodeVatPaidTo", m_countryCodeVatPaidTo},
        {"amountTaxed", m_amountSource.getAmountTaxed()},
        {"amountTaxes", m_amountSource.getTaxes()},
        {"taxSource", static_cast<int>(m_taxSource)},
        {"taxDeclaringCountryCode", m_taxDeclaringCountryCode},
        {"taxScheme", static_cast<int>(m_taxScheme)},
        {"taxJurisdictionLevel", static_cast<int>(m_taxJurisdictionLevel)},
        {"saleType", static_cast<int>(m_saleType)},
        {"vatTerritoryFrom", m_vatTerritoryFrom},
        {"vatTerritoryTo", m_vatTerritoryTo},
        {"taxesComputed", m_AmountTaxesComputed}
    };
}

Activity Activity::fromJson(const QJsonObject &json)
{
    return Activity(
        json["eventId"].toString(),
        json["activityId"].toString(),
        json["subActivityId"].toString(),
        QDateTime::fromString(json["dateTime"].toString(), Qt::ISODate),
        json["currency"].toString(),
        json["countryCodeFrom"].toString(),
        json["countryCodeTo"].toString(),
        json["countryCodeVatPaidTo"].toString(),
        Amount(json["amountTaxed"].toDouble(), json["amountTaxes"].toDouble()),
        static_cast<TaxSource>(json["taxSource"].toInt()),
        json["taxDeclaringCountryCode"].toString(),
        static_cast<TaxScheme>(json["taxScheme"].toInt()),
        static_cast<TaxJurisdictionLevel>(json["taxJurisdictionLevel"].toInt()),
        static_cast<SaleType>(json["saleType"].toInt()),
        json["vatTerritoryFrom"].toString(),
        json["vatTerritoryTo"].toString(),
        json["taxesComputed"].toDouble()
    );
}
