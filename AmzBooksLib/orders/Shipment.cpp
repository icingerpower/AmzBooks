#include "books/TaxResolver.h"
#include "books/VatResolver.h"

#include "Shipment.h"
#include <QJsonArray>

Shipment::Shipment(QList<Activity> activities)
    : m_activities(std::move(activities))
{
}

double Shipment::getTotalTaxed() const
{
    double total = 0.;
    for (const auto &activity : m_activities)
    {
        total += activity.getAmountTaxed();
    }
    return total;
}

void Shipment::computeTax(
        const TaxResolver *taxResolver
        , const VatResolver *vatResolver
        , const QString &vatTerritoryFrom
        , const QString &vatTerritoryTo)
{
    for (auto &activity : m_activities)
    {
        activity.computeTax(taxResolver, vatResolver, vatTerritoryFrom, vatTerritoryTo);
    }
}

const QString &Shipment::getId() const noexcept
{
    // Assuming at least one activity and they share the same base ID or the first one represents the group
    static QString empty;
    if (m_activities.isEmpty()) {
        return empty;
    }
    return m_activities.first().getActivityId();
}

const QList<Activity>& Shipment::getActivities() const noexcept
{
    return m_activities;
}

QJsonObject Shipment::toJson() const
{
    QJsonArray arr;
    for (const auto &act : m_activities) {
        arr.append(act.toJson());
    }
    QJsonObject obj{
        {"activities", arr},
        {"isWrongIfConflict", m_isWrongIfConflict}
    };
    return obj;
}

Shipment Shipment::fromJson(const QJsonObject &json)
{
    QList<Activity> list;
    if (json.contains("activities")) {
        QJsonArray arr = json["activities"].toArray();
        for (const auto &val : arr) {
            list.append(Activity::fromJson(val.toObject()));
        }
    } else if (json.contains("activity")) {
        // Migration / Backward compatibility for old format in DB or source
        list.append(Activity::fromJson(json["activity"].toObject()));
    }
    Shipment s(list);
    if (json.contains("isWrongIfConflict")) {
        s.setIsWrongIfConflict(json["isWrongIfConflict"].toBool());
    }
    return s;
}

