#ifndef ACTIVITYSOURCE_H
#define ACTIVITYSOURCE_H

#include <QString>

#include "ActivitySourceType.h"

struct ActivitySource {
    ActivitySourceType type;
    QString channel; // Exemple Amazon or Temu
    QString subchannel; // Exemple amazon.fr or amazon.de
    QString reportOrMethode; // Report or method name that allowed to retried the information
};

#endif // ACTIVITYSOURCE_H
