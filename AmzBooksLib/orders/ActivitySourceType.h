#ifndef ACTIVITYSOURCETYPE_H
#define ACTIVITYSOURCETYPE_H

#include <QtGlobal>
#include <QHash>

enum class ActivitySourceType : quint8 {
    Report,
    API
};

inline uint qHash(ActivitySourceType key, uint seed = 0)
{
    return ::qHash(static_cast<int>(key), seed);
}

QString toString(ActivitySourceType type);
ActivitySourceType toActivitySourceType(const QString &str);

#endif // ACTIVITYSOURCETYPE_H
