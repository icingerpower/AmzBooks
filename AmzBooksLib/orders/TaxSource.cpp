#include "TaxSource.h"
#include <QHash>
#include <QDebug>

static const QHash<TaxSource, QString> ENUM_STRING = {
    {TaxSource::Unknown, "Unknown"},
    {TaxSource::MarketplaceProvided, "MarketplaceProvided"},
    {TaxSource::SelfComputed, "SelfComputed"},
    {TaxSource::ManualOverride, "ManualOverride"}
};

static const QHash<QString, TaxSource> STRING_ENUM = [] {
    QHash<QString, TaxSource> map;
    for (auto it = ENUM_STRING.begin(); it != ENUM_STRING.end(); ++it) {
        map.insert(it.value(), it.key());
    }
    return map;
}();

QString taxSourceToString(TaxSource type)
{
    return ENUM_STRING.value(type, "Unknown");
}

TaxSource toTaxSource(const QString &str)
{
    return STRING_ENUM.value(str, TaxSource::Unknown);
}

QDebug operator<<(QDebug dbg, TaxSource type)
{
    QDebugStateSaver saver(dbg);
    dbg.nospace() << "TaxSource::" << taxSourceToString(type);
    return dbg;
}


