#ifndef TAXSOURCE_H
#define TAXSOURCE_H

#include <QtGlobal>

enum class TaxSource : quint8
{
    Unknown,
    MarketplaceProvided,
    SelfComputed,
    ManualOverride
};

#endif // TAXSOURCE_H
