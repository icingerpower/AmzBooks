#ifndef TAXSOURCE_H
#define TAXSOURCE_H

#include <QtGlobal>
#include <QHash>
#include <QDebug>

enum class TaxSource : quint8
{
    Unknown,
    MarketplaceProvided,
    SelfComputed,
    ManualOverride
};

inline uint qHash(TaxSource key, uint seed = 0)
{
    return ::qHash(static_cast<int>(key), seed);
}

QString taxSourceToString(TaxSource type);
TaxSource toTaxSource(const QString &str);
QDebug operator<<(QDebug dbg, TaxSource type);

#endif // TAXSOURCE_H
