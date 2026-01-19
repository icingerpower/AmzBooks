#ifndef TAXJURISDICTIONLEVEL_H
#define TAXJURISDICTIONLEVEL_H

#include <QtGlobal>

enum class TaxJurisdictionLevel : quint8 {
    Unknown,

    Supranational,   // e.g., EU-wide rules bucket (rare)
    Country,         // Typical in EU VAT (FR/DE/IT…)
    Region,          // State/Land/Province level if relevant
    City,            // Rare, but exists for some local taxes (mostly non-VAT)
    PostalCode,      // Rate depends on postal code (some tax systems)
    Territory,       // Special VAT territory override (e.g., IT-LIVIGNO)
    Zone             // Custom zones (e.g., customs zones, tax-free zones)
};

#endif // TAXJURISDICTIONLEVEL_H
