#ifndef LINEITEM_H
#define LINEITEM_H

#include <QString>

#include <optional>

#include "Amount.h"

// LineItem = one priced component of a shipment/order (product, shipping, fee, or discount)
// with quantity and VAT rate, able to compute totals and accept a small VAT adjustment for reconciliation.

class LineItem
{
public:
    LineItem(QString sku,
             QString name,
             double taxedAmount,
             double vatRate,
             int quantity);

    const QString& getSku() const noexcept;
    const QString& getName() const noexcept;
    int getQuantity() const noexcept;
    double getAmountTaxed() const noexcept;
    double getTaxes() const noexcept;
    double getTotalTaxed() const noexcept;
    double getTotalTaxes() const noexcept;

    void adjustTaxes(double delta);

protected:
    QString m_sku;
    QString m_name;
    int m_quantity;
    Amount m_amount;
};

#endif // LINEITEM_H
