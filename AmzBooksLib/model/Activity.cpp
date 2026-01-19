#include "Activity.h"

Activity::Activity(QString eventId,
                   QDateTime dateTime,
                   QString currency,
                   QString countryCodeFrom,
                   QString countryCodeTo,
                   double amountTaxed,
                   double amountTaxes,
                   TaxSource taxSource,
                   QString taxDeclaringCountryCode,
                   TaxScheme taxScheme,
                   TaxJurisdictionLevel taxJurisdictionLevel,
                   QString vatTerritoryFrom,
                   QString vatTerritoryTo)
    : m_eventId(std::move(eventId))
    , m_dateTime(std::move(dateTime))
    , m_currency(std::move(currency))
    , m_countryCodeFrom(std::move(countryCodeFrom))
    , m_countryCodeTo(std::move(countryCodeTo))
    , m_amountTaxed(amountTaxed)
    , m_AmountTaxesSource(amountTaxes)
    , m_taxSource(taxSource)
    , m_taxDeclaringCountryCode(std::move(taxDeclaringCountryCode))
    , m_taxScheme(taxScheme)
    , m_taxJurisdictionLevel(taxJurisdictionLevel)
    , m_vatTerritoryFrom(std::move(vatTerritoryFrom))
    , m_vatTerritoryTo(std::move(vatTerritoryTo))
{
    // NOTE: using double for money is risky (rounding/reconciliation).
    // Consider switching to minor units (qint64 cents) as soon as possible.
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

double Activity::getAmountUntaxed() const noexcept
{
    return getAmountTaxed() - getAmountTaxes();
}

double Activity::getAmountTaxed() const noexcept
{
    return m_amountTaxed;
}

double Activity::getAmountTaxes() const noexcept
{
    if (m_taxSource == TaxSource::ManualOverride
            || m_taxSource == TaxSource::SelfComputed)
    {
        return m_AmountTaxesComputed;
    }
    return m_AmountTaxesSource;
}

double Activity::getAmountTaxesSource() const noexcept
{
    return m_AmountTaxesSource;
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

const QString& Activity::getVatTerritoryFrom() const noexcept
{
    return m_vatTerritoryFrom;
}

const QString& Activity::getVatTerritoryTo() const noexcept
{
    return m_vatTerritoryTo;
}
