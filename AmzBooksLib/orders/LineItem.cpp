#include "LineItem.h"

Result<LineItem> LineItem::create(QString sku,
                                  QString name,
                                  double taxedAmount,
                                  double vatRate,
                                  double quantity)
{
    Result<LineItem> result;

    if (name.isEmpty()) {
        result.errors.append(ValidationError{QObject::tr("Name"), QObject::tr("Name must not be empty")});
    }
    if (taxedAmount == 0.0) {
        result.errors.append(ValidationError{QObject::tr("taxedAmount"), QObject::tr("Taxed amount must not be 0")});
    }
    if (quantity <= 0) {
        result.errors.append(ValidationError{QObject::tr("quantity"), QObject::tr("Quantity must be greater than 0")});
    }

    if (result.errors.isEmpty()) {
        result.value.emplace(LineItem(std::move(sku),
                                      std::move(name),
                                      taxedAmount,
                                      vatRate,
                                      quantity));
    }

    return result;
}

LineItem::LineItem(
        QString sku,
        QString name,
        double taxedAmount,
        double vatRate,
        double quantity)
    : m_sku(std::move(sku))
    , m_name(std::move(name))
    , m_quantity(quantity)
    , m_amount(taxedAmount, taxedAmount - (taxedAmount / (1 + vatRate)))
{
}

const QString& LineItem::getSku() const noexcept
{
    return m_sku;
}

const QString& LineItem::getName() const noexcept
{
    return m_name;
}

void LineItem::setName(const QString &name)
{
    m_name = name;
}

double LineItem::getQuantity() const noexcept
{
    return m_quantity;
}

double LineItem::getAmountUntaxed() const noexcept
{
    return m_amount.getAmountUntaxed();
}

double LineItem::getAmountTaxed() const noexcept
{
    return m_amount.getAmountTaxed();
}

double LineItem::getTaxes() const noexcept
{
    return m_amount.getTaxes();
}

double LineItem::getTotalUntaxed() const noexcept
{
    return m_amount.getAmountUntaxed() * m_quantity;
}

double LineItem::getTotalTaxed() const noexcept
{
    return m_amount.getAmountTaxed() * m_quantity;
}

double LineItem::getTotalTaxes() const noexcept
{
    return m_amount.getTaxes() * m_quantity;
}

void LineItem::adjustTaxes(double delta)
{
    double newTax = m_amount.getTaxes() + delta;
    m_amount = Amount(m_amount.getAmountTaxed(), newTax);
}

LineItem::LineItem(QString sku,
                   QString name,
                   double quantity,
                   Amount amount)
    : m_sku(std::move(sku))
    , m_name(std::move(name))
    , m_quantity(quantity)
    , m_amount(std::move(amount))
{
}

QJsonObject LineItem::toJson() const
{
    return QJsonObject{
        {"sku", m_sku},
        {"name", m_name},
        {"quantity", m_quantity},
        {"amountTaxed", m_amount.getAmountTaxed()},
        {"amountTaxes", m_amount.getTaxes()}
    };
}

LineItem LineItem::fromJson(const QJsonObject &json)
{
    return LineItem(
        json["sku"].toString(),
        json["name"].toString(),
        json["quantity"].toDouble(),
        Amount(json["amountTaxed"].toDouble(), json["amountTaxes"].toDouble())
    );
}
