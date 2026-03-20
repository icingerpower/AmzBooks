#include "CountriesEu.h"

const QString CountriesEu::AT = "AT";
const QString CountriesEu::BE = "BE";
const QString CountriesEu::BG = "BG";
const QString CountriesEu::CY = "CY";
const QString CountriesEu::CZ = "CZ";
const QString CountriesEu::DE = "DE";
const QString CountriesEu::DK = "DK";
const QString CountriesEu::EE = "EE";
const QString CountriesEu::ES = "ES";
const QString CountriesEu::FI = "FI";
const QString CountriesEu::FR = "FR";
const QString CountriesEu::GR = "GR";
const QString CountriesEu::HR = "HR";
const QString CountriesEu::HU = "HU";
const QString CountriesEu::IE = "IE";
const QString CountriesEu::IT = "IT";
const QString CountriesEu::LT = "LT";
const QString CountriesEu::LU = "LU";
const QString CountriesEu::LV = "LV";
const QString CountriesEu::MT = "MT";
const QString CountriesEu::NL = "NL";
const QString CountriesEu::PL = "PL";
const QString CountriesEu::PT = "PT";
const QString CountriesEu::RO = "RO";
const QString CountriesEu::SE = "SE";
const QString CountriesEu::SI = "SI";
const QString CountriesEu::SK = "SK";

const QString CountriesEu::GB = "GB";
const QString CountriesEu::MC = "MC";
const QString CountriesEu::XI = "XI";

bool CountriesEu::isEuMember(const QString &countryCode, const QDate &date)
{
    if (countryCode == GB) {
        // Brexit: GB is EU member until 2020-12-31 inclusive.
        return date < QDate(2021, 1, 1);
    }

    return all().contains(countryCode);
}

const QSet<QString>& CountriesEu::all()
{
    static const QSet<QString> euCountries = {
        AT, BE, BG, CY, CZ, DE, DK, EE, ES, FI, FR, GR, HR, HU, IE, IT, LT, LU, LV, MT, NL, PL, PT, RO, SE, SI, SK, 
        MC, XI 
        // MC (Monaco) and XI (Northern Ireland) are treated as EU for goods.
    };
    return euCountries;
}

const QStringList& CountriesEu::getAmazonPanEuCountryCodes()
{
    static const QStringList list = {DE, FR, IT, ES, PL, GB, CZ}; // Includes GB (Pan-EU legacy/scope)
    return list;
}

QStringList CountriesEu::getCountries()
{
    return {
        AT, BE, BG, CY, CZ, DE, DK, EE, ES, FI, 
        FR, GB, GR, HR, HU, IE, IT, LT, LU, LV, 
        MT, NL, PL, PT, RO, SE, SI, SK
    };
}

QStringList CountriesEu::getCurrencies()
{
    return {
        "EUR", "GBP", "BGN", "CZK", "DKK", "HUF", "PLN", "RON", "SEK"
    };
}

const QSet<QString>& CountriesEu::getCurrenciesWorld()
{
    static const QSet<QString> currencies = {
        "EUR", "GBP", "USD", "CAD", "AUD", "NZD", "CHF",
        "NOK", "SEK", "DKK",
        "BGN", "CZK", "HUF", "PLN", "RON",
        "JPY", "CNY", "HKD", "SGD",
        "TRY", "MXN", "BRL"
    };
    return currencies;
}
const QStringList CountriesEu::DEFAULT_AMAZON_SITES = {
    "amazon.ca",
    "amazon.com",
    "amazon.com.be",
    "amazon.com.mx",
    "amazon.de",
    "amazon.es",
    "amazon.fr",
    "amazon.ie",
    "amazon.it",
    "amazon.nl",
    "amazon.pl",
    "amazon.se",
    "amazon.tr",
    "amazon.co.uk",
    "amazon.co.jp",
    "amazon.com.au"
};

QString CountriesEu::amazonSiteFromMarketplaceCode(const QString &marketplaceCode)
{
    const QString normalized = marketplaceCode.toLower().replace(QChar('_'), QChar('.'));
    for (const QString &site : DEFAULT_AMAZON_SITES) {
        if (site.endsWith(normalized)) {
            return site;
        }
    }
    Q_ASSERT(false);
    return {};
}

QString CountriesEu::toCode(const QString &nameOrCode)
{
    QString trimmed = nameOrCode.trimmed();
    if (trimmed.isEmpty())
        return trimmed;

    // If already a 2-letter code, normalise to upper-case and return.
    if (trimmed.length() == 2)
        return trimmed.toUpper();

    static const QMap<QString, QString> nameMap = {
        // --- English names (all 27 EU members + common extras) ---
        {"austria",              "AT"},
        {"belgium",              "BE"},
        {"bulgaria",             "BG"},
        {"cyprus",               "CY"},
        {"czech republic",       "CZ"},
        {"czechia",              "CZ"},
        {"germany",              "DE"},
        {"denmark",              "DK"},
        {"estonia",              "EE"},
        {"spain",                "ES"},
        {"finland",              "FI"},
        {"france",               "FR"},
        {"greece",               "GR"},
        {"croatia",              "HR"},
        {"hungary",              "HU"},
        {"ireland",              "IE"},
        {"italy",                "IT"},
        {"lithuania",            "LT"},
        {"luxembourg",           "LU"},
        {"latvia",               "LV"},
        {"malta",                "MT"},
        {"netherlands",          "NL"},
        {"poland",               "PL"},
        {"portugal",             "PT"},
        {"romania",              "RO"},
        {"sweden",               "SE"},
        {"slovenia",             "SI"},
        {"slovakia",             "SK"},
        // Former / special
        {"united kingdom",       "GB"},
        {"great britain",        "GB"},
        {"uk",                   "GB"},
        {"northern ireland",     "XI"},
        {"monaco",               "MC"},
        // Non-EU frequently encountered
        {"switzerland",          "CH"},
        {"norway",               "NO"},
        {"united states",        "US"},
        {"united states of america", "US"},
        {"usa",                  "US"},
        {"canada",               "CA"},
        {"japan",                "JP"},
        {"china",                "CN"},
        {"turkey",               "TR"},
        // --- French names (all 27 EU members + common extras) ---
        {"allemagne",            "DE"},
        {"autriche",             "AT"},
        {"belgique",             "BE"},
        {"bulgarie",             "BG"},
        {"chypre",               "CY"},
        {"croatie",              "HR"},
        {"danemark",             "DK"},
        {"espagne",              "ES"},
        {"estonie",              "EE"},
        {"finlande",             "FI"},
        {"grèce",                "GR"},
        {"grece",                "GR"},
        {"hongrie",              "HU"},
        {"irlande",              "IE"},
        {"italie",               "IT"},
        {"lettonie",             "LV"},
        {"lituanie",             "LT"},
        {"luxembourg",           "LU"}, // same in FR/EN
        {"malte",                "MT"},
        {"pays-bas",             "NL"},
        {"pays bas",             "NL"},
        {"pologne",              "PL"},
        {"portugal",             "PT"}, // same in FR/EN
        {"republique tcheque",   "CZ"},
        {"république tchèque",   "CZ"},
        {"republique tchèque",   "CZ"},
        {"roumanie",             "RO"},
        {"slovaquie",            "SK"},
        {"slovénie",             "SI"},
        {"slovenie",             "SI"},
        {"suède",                "SE"},
        {"suede",                "SE"},
        // Former / special
        {"royaume-uni",          "GB"},
        {"royaume uni",          "GB"},
        {"irlande du nord",      "XI"},
        // Non-EU frequently encountered
        {"suisse",               "CH"},
        {"norvège",              "NO"},
        {"norvege",              "NO"},
        {"états-unis",           "US"},
        {"etats-unis",           "US"},
        {"états unis",           "US"},
        {"etats unis",           "US"},
        {"chine",                "CN"},
        {"japon",                "JP"},
        {"turquie",              "TR"},
        {"canada",               "CA"}, // same in FR/EN
    };

    QString lower = trimmed.toLower();
    if (nameMap.contains(lower))
        return nameMap[lower];

    qWarning() << "CountriesEu::toCode: unknown country name:" << nameOrCode;
    return trimmed;
}
