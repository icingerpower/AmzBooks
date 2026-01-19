#include "LineItem.h"

LineItem::LineItem(
        QString sku,
        QString name,
        double taxedAmount,
        double vatRate,
        int quantity)
    : m_sku(std::move(sku))
    , m_name(std::move(name))
    , m_quantity(quantity)
    , m_amount(taxedAmount, taxedAmount * vatRate)
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

int LineItem::getQuantity() const noexcept
{
    return m_quantity;
}

double LineItem::getAmountTaxed() const noexcept
{
    return m_amount.getAmountTaxed();
}

double LineItem::getTaxes() const noexcept
{
    return m_amount.getTaxes();
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
