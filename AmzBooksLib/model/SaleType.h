#ifndef SALETYPE_H
#define SALETYPE_H

#include <QHash>

enum class SaleType {
    Products,
    Service,
    InventoryMove
};

inline uint qHash(SaleType key, uint seed = 0)
{
    return ::qHash(static_cast<int>(key), seed);
}

QString toString(SaleType type);
SaleType toSaleType(const QString &str);


#endif // SALETYPE_H
