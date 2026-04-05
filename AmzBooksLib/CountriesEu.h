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
    
    // Returns list of all EU countries plus GB
    static QStringList getCountries();
    
    // Returns list of currencies for GB and EU countries
    static QStringList getCurrencies();

    // Returns a broader set of world currencies (western + major trading partners).
    // Returns a QSet for O(1) membership testing.
    static const QSet<QString>& getCurrenciesWorld();

    static const QStringList DEFAULT_AMAZON_SITES;

    // Returns the canonical Amazon site name (e.g. "amazon.com.mx") for a
    // marketplace code from payment filenames (e.g. "com_mx"). Underscores are
    // treated as dots before the lookup against DEFAULT_AMAZON_SITES, so that
    // "co_uk" → "amazon.co.uk" and "com_mx" → "amazon.com.mx" (not "amazon.mx").
    // Returns an empty string if no matching site is found.
    static QString amazonSiteFromMarketplaceCode(const QString &marketplaceCode);

    // Converts a country name (in English or French) or an existing 2-letter ISO code
    // to the ISO 3166-1 alpha-2 code. Case-insensitive.
    // If the input is already a 2-letter code it is returned uppercased.
    // Returns the trimmed input unchanged (with a warning) when the name is unknown.
    static QString toCode(const QString &nameOrCode);

    // Converts an ISO 3166-1 alpha-2 code to its canonical French name (e.g. "DE" → "Allemagne").
    // Returns the uppercased code unchanged (with a warning) when the code is unknown.
    static QString toFrenchName(const QString &code);
};

#endif // COUNTRIESEU_H
