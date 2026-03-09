#ifndef LINEITEM_H
#define LINEITEM_H

#include <QString>

#include <optional>

#include "Amount.h"
#include "Result.h"
#include <QJsonObject>

// LineItem = one priced component of a shipment/order (product, shipping, fee, or discount)
// with quantity and VAT rate, able to compute totals and accept a small VAT adjustment for reconciliation.

class LineItem
{
public:
    static Result<LineItem> create(QString sku,
                                   QString name,
                                   double taxedAmount,
                                   double vatRate,
                                   double quantity);

    const QString& getSku() const noexcept;
    const QString& getName() const noexcept;
    void setName(const QString &name);
    double getQuantity() const noexcept;
    double getAmountUntaxed() const noexcept;
    double getAmountTaxed() const noexcept;
    double getTaxes() const noexcept;
    double getTotalUntaxed() const noexcept;
    double getTotalTaxed() const noexcept;
    double getTotalTaxes() const noexcept;

    void adjustTaxes(double delta);

    static LineItem fromJson(const QJsonObject &json);
    QJsonObject toJson() const;

protected:
    LineItem(QString sku,
             QString name,
             double quantity,
             Amount amount);

    LineItem(QString sku,
             QString name,
             double taxedAmount,
             double vatRate,
             double quantity);

    QString m_sku;
    QString m_name;
    double m_quantity;
    Amount m_amount;
};

#endif // LINEITEM_H
