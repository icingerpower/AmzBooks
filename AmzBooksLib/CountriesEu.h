#ifndef COUNTRIESEU_H
#define COUNTRIESEU_H

#include <QString>
#include <QSet>
#include <QDate>

class CountriesEu
{
public:
    // EU Member States (Iso2 codes)
    static const QString AT;
    static const QString BE;
    static const QString BG;
    static const QString CY;
    static const QString CZ;
    static const QString DE;
    static const QString DK;
    static const QString EE;
    static const QString ES;
    static const QString FI;
    static const QString FR;
    static const QString GR;
    static const QString HR;
    static const QString HU;
    static const QString IE;
    static const QString IT;
    static const QString LT;
    static const QString LU;
    static const QString LV;
    static const QString MT;
    static const QString NL;
    static const QString PL;
    static const QString PT;
    static const QString RO;
    static const QString SE;
    static const QString SI;
    static const QString SK;
    
    // Special cases / former members
    static const QString GB; // United Kingdom (Member until 2020-12-31)
    static const QString MC; // Monaco (Treated as FR for VAT)
    static const QString XI; // Northern Ireland (Protocol)

    // Check if a country code refers to an EU member state at a given date.
    static bool isEuMember(const QString &countryCode, const QDate &date);

    // Returns a list of all current EU member states (as of latest date, excluding former ones if strictly current, but for iteration purposes often we want the set used in rules).
    // This returns the static set used for checking.
    static const QSet<QString>& all();

    static const QStringList& getAmazonPanEuCountryCodes();
};

#endif // COUNTRIESEU_H
