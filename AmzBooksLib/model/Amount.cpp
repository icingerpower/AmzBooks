#include "Amount.h"

Amount::Amount(double amountTaxed, double taxes)
    : m_amountTaxed(amountTaxed)
    , m_taxes(taxes)
{
}

double Amount::getAmountUntaxed() const noexcept
{
    return getAmountTaxed() - getTaxes();
}

double Amount::getAmountTaxed() const noexcept
{
    return m_amountTaxed;
}

double Amount::getTaxes() const noexcept
{
    return m_taxes;
}
