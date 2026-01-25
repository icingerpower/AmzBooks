#include "Shipment.h"
#include <QJsonArray>

Shipment::Shipment(QList<Activity> activities)
    : m_activities(std::move(activities))
{
}

const QString &Shipment::getId() const noexcept
{
    // Assuming at least one activity and they share the same base ID or the first one represents the group
    static QString empty;
    if (m_activities.isEmpty()) return empty;
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
    return QJsonObject{
        {"activities", arr}
    };
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
    return Shipment(list);
}

