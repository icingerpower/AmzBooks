#include "ActivitySourceType.h"
#include <QHash>

static const QHash<ActivitySourceType, QString> ENUM_STRING = {
    {ActivitySourceType::Report, "Report"},
    {ActivitySourceType::API, "API"}
};

static const QHash<QString, ActivitySourceType> STRING_ENUM = [] {
    QHash<QString, ActivitySourceType> map;
    for (auto it = ENUM_STRING.begin(); it != ENUM_STRING.end(); ++it) {
        map.insert(it.value(), it.key());
    }
    return map;
}();

QString toString(ActivitySourceType type)
{
    return ENUM_STRING.value(type, "Unknown");
}

ActivitySourceType toActivitySourceType(const QString &str)
{
    return STRING_ENUM.value(str, ActivitySourceType::Report);
}
