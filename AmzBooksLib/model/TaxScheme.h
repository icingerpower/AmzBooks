#ifndef TAXSCHEME_H
#define TAXSCHEME_H

#include <QtGlobal>

enum class TaxScheme : quint8 {
    Unknown,

    DomesticVat,              // Standard in-country VAT
    EuOssUnion,               // EU OSS (Union scheme) for B2C intra-EU supplies
    EuOssNonUnion,            // EU OSS (Non-Union scheme) for non-EU suppliers (rare for your case)
    EuIoss,                   // IOSS for distance sales of imported goods (<=150 EUR, if applicable)

    ImportVat,                // Import VAT (at border) when available
    ReverseChargeImport,      // Reverse-charge / postponed accounting for import VAT (autoliquidation import)
    ReverseChargeDomestic,    // Domestic reverse-charge (B2B specific sectors/countries)

    MarketplaceDeemedSupplier,// Marketplace is deemed supplier / marketplace collects VAT
    Exempt,                   // Exempt supply (no VAT)
    OutOfScope                // Outside scope of VAT (territories excluded, etc.)
};

#endif // TAXSCHEME_H
