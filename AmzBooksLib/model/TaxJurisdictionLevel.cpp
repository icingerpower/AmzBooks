#include "TaxJurisdictionLevel.h"
#include <QHash>
#include <QDebug>

static const QHash<TaxJurisdictionLevel, QString> ENUM_STRING = {
    {TaxJurisdictionLevel::Unknown, "Unknown"},
    {TaxJurisdictionLevel::Supranational, "Supranational"},
    {TaxJurisdictionLevel::Country, "Country"},
    {TaxJurisdictionLevel::Region, "Region"},
    {TaxJurisdictionLevel::City, "City"},
    {TaxJurisdictionLevel::PostalCode, "PostalCode"},
    {TaxJurisdictionLevel::Territory, "Territory"},
    {TaxJurisdictionLevel::Zone, "Zone"}
};

static const QHash<QString, TaxJurisdictionLevel> STRING_ENUM = [] {
    QHash<QString, TaxJurisdictionLevel> map;
    for (auto it = ENUM_STRING.begin(); it != ENUM_STRING.end(); ++it) {
        map.insert(it.value(), it.key());
    }
    return map;
}();

QString taxJurisdictionLevelToString(TaxJurisdictionLevel type)
{
    return ENUM_STRING.value(type, "Unknown");
}

TaxJurisdictionLevel toTaxJurisdictionLevel(const QString &str)
{
    return STRING_ENUM.value(str, TaxJurisdictionLevel::Unknown);
}

QDebug operator<<(QDebug dbg, TaxJurisdictionLevel type)
{
    QDebugStateSaver saver(dbg);
    dbg.nospace() << "TaxJurisdictionLevel::" << taxJurisdictionLevelToString(type);
    return dbg;
}
