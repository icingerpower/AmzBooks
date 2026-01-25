#ifndef ACTIVITYSOURCE_H
#define ACTIVITYSOURCE_H

#include <QString>

#include "ActivitySourceType.h"

struct ActivitySource {
    ActivitySourceType type;
    QString channel; // Exemple Amazon or Temu
    QString subchannel; // Exemple amazon.fr or amazon.de
    QString reportOrMethode; // Report or method name that allowed to retried the information

    bool operator==(const ActivitySource &other) const {
        return type == other.type &&
               channel == other.channel &&
               subchannel == other.subchannel &&
               reportOrMethode == other.reportOrMethode;
    }
};

inline uint qHash(const ActivitySource &key, uint seed = 0) {
    return qHash(static_cast<int>(key.type), seed) ^
           qHash(key.channel, seed) ^
           qHash(key.subchannel, seed) ^
           qHash(key.reportOrMethode, seed);
}

#endif // ACTIVITYSOURCE_H
