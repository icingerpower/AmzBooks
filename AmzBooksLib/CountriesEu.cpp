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
