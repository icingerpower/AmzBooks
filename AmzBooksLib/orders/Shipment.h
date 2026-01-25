#ifndef SHIPMENT_H
#define SHIPMENT_H

#include "books/Activity.h"
#include "LineItem.h"
#include <QJsonObject>
#include <QList>

// Shipment = a business shipment event containing one aggregated Activity (overall net/VAT context)
// plus detailed LineItems, with a reconciliation step to minimally adjust item VAT so the sum
// matches the Activity totals.

class Shipment
{
public:
    explicit Shipment(QList<Activity> activities);
    const QString& getId() const noexcept;

    const QList<Activity>& getActivities() const noexcept;

    static Shipment fromJson(const QJsonObject &json);
    QJsonObject toJson() const;

protected:
    QList<Activity> m_activities;
};

#endif // SHIPMENT_H
