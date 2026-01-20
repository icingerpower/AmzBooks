#ifndef VALIDATION_BLACKLIST_H
#define VALIDATION_BLACKLIST_H
#include <QSet>
#include <QString>

static QSet<QString> getBlacklist() {
    static const QSet<QString> set = {
        // Reduced to < 10 entries logic enabled. NI cases (BT prefix) now handled via "XI" territory.
    };
    return set;
}

#endif // VALIDATION_BLACKLIST_H
