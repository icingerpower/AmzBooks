#include "SaleType.h"

static const QHash<SaleType, QString> SALETYPE_STRING = {
    {SaleType::Products, "Products"},
    {SaleType::Service, "Service"},
    {SaleType::InventoryMove, "InventoryMove"}
};

static const QHash<QString, SaleType> STRING_SALETYPE = [] {
    QHash<QString, SaleType> map;
    for (auto it = SALETYPE_STRING.begin(); it != SALETYPE_STRING.end(); ++it) {
        map.insert(it.value(), it.key());
    }
    return map;
}();

QString toString(SaleType type)
{
    return SALETYPE_STRING.value(type, "Unknown");
}

SaleType toSaleType(const QString &str)
{
    return STRING_SALETYPE.value(str, SaleType::Products); // Default or explicit unknown?
}
