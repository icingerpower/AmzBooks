#ifndef SHIPMENT_H
#define SHIPMENT_H

#include "books/Activity.h"
#include "LineItem.h"
#include <QJsonObject>
#include <QList>

class TaxResolver;
class VatResolver;
// Shipment = a business shipment event containing one aggregated Activity (overall net/VAT context)
// plus detailed LineItems, with a reconciliation step to minimally adjust item VAT so the sum
// matches the Activity totals.

class Shipment
{
public:
    explicit Shipment(QList<Activity> activities, QString customerAccount, bool isGrouped); // No default value
    virtual ~Shipment() = default;
    double getTotalTaxed() const;
    void computeTax(const TaxResolver *taxResolver
                    , const VatResolver *vatResolver
                    , const QString &vatTerritoryFrom
                    , const QString &vatTerritoryTo);
    const QString& getId() const noexcept;

    const QList<Activity>& getActivities() const noexcept;

    static Shipment fromJson(const QJsonObject &json);
    QJsonObject toJson() const;

    bool isWrongIfConflict() const { return m_isWrongIfConflict; }
    void setIsWrongIfConflict(bool val) { m_isWrongIfConflict = val; }

    bool isGrouped() const;
    void setIsGrouped(bool val) { m_isGrouped = val; }

    const QString &customerAccount() const noexcept { return m_customerAccount; }
    void setCustomerAccount(const QString &val) { m_customerAccount = val; }

protected:
    QList<Activity> m_activities;
    bool m_isWrongIfConflict = false;
    bool m_isGrouped = true;
    QString m_customerAccount;
};

#endif // SHIPMENT_H
