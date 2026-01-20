#include "TaxScheme.h"
#include <QHash>
#include <QDebug>

static const QHash<TaxScheme, QString> ENUM_STRING = {
    {TaxScheme::Unknown, "Unknown"},
    {TaxScheme::DomesticVat, "DomesticVat"},
    {TaxScheme::EuOssUnion, "EuOssUnion"},
    {TaxScheme::EuOssNonUnion, "EuOssNonUnion"},
    {TaxScheme::EuIoss, "EuIoss"},
    {TaxScheme::ImportVat, "ImportVat"},
    {TaxScheme::ReverseChargeImport, "ReverseChargeImport"},
    {TaxScheme::ReverseChargeDomestic, "ReverseChargeDomestic"},
    {TaxScheme::MarketplaceDeemedSupplier, "MarketplaceDeemedSupplier"},
    {TaxScheme::Exempt, "Exempt"},
    {TaxScheme::OutOfScope, "OutOfScope"}
};

static const QHash<QString, TaxScheme> STRING_ENUM = [] {
    QHash<QString, TaxScheme> map;
    for (auto it = ENUM_STRING.begin(); it != ENUM_STRING.end(); ++it) {
        map.insert(it.value(), it.key());
    }
    return map;
}();

QString taxSchemeToString(TaxScheme type)
{
    return ENUM_STRING.value(type, "Unknown");
}

TaxScheme toTaxScheme(const QString &str)
{
    return STRING_ENUM.value(str, TaxScheme::Unknown);
}

QDebug operator<<(QDebug dbg, TaxScheme type)
{
    QDebugStateSaver saver(dbg);
    dbg.nospace() << "TaxScheme::" << taxSchemeToString(type);
    return dbg;
}
