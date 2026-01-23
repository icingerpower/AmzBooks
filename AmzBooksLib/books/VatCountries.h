#ifndef VATCOUNTRIES_H
#define VATCOUNTRIES_H

#include <QString>

#include "model/TaxScheme.h"

struct VatCountries{
    TaxScheme taxScheme;
    QString countryCodeFrom;
    QString countryCodeTo;

    bool operator==(const VatCountries &other) const {
        return taxScheme == other.taxScheme &&
               countryCodeFrom == other.countryCodeFrom &&
               countryCodeTo == other.countryCodeTo;
    }
};

inline uint qHash(const VatCountries &key, uint seed = 0) {
    return qHash(key.taxScheme, seed) ^ qHash(key.countryCodeFrom, seed) ^ qHash(key.countryCodeTo, seed);
}

#endif // VATCOUNTRIES_H
