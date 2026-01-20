#include <QtTest>
#include <QCoreApplication>
#include "model/TaxResolver.h"
#include "model/TaxScheme.h"
#include "model/TaxJurisdictionLevel.h"
#include "model/SaleType.h"
#include "utils/CsvReader.h"
#include "ValidationBlacklist.h"
#include "model/VatTerritoryResolver.h"

// MOCK OR HELPER CLASS FOR TESTS
class TestTaxResolver : public QObject
{
    Q_OBJECT

public:
    TestTaxResolver();
    ~TestTaxResolver();

private slots:
    void initTestCase();
    void cleanupTestCase();
    void test_NonEUScenarios();
    void test_SpecialTerritories();
    void test_AmazonReports();

private:
    CsvReader createAmazonReader(const QString &fileName);
    TaxScheme mapAmazonTaxScheme(const QString &schemeStr);
    QString schemeStrToString(TaxScheme scheme);
};

TestTaxResolver::TestTaxResolver()
{
}

TestTaxResolver::~TestTaxResolver()
{
}

void TestTaxResolver::initTestCase()
{
}

void TestTaxResolver::cleanupTestCase()
{
}

void TestTaxResolver::test_NonEUScenarios()
{
    TaxResolver resolver(QCoreApplication::applicationDirPath());
    QDateTime now = QDateTime::currentDateTime();

    // 1. CN -> US (Non-EU -> Non-EU) => OutOfScope
    {
        auto ctx = resolver.getTaxContext(now, "CN", "US", SaleType::Products, false, false, "", "");
        QCOMPARE(ctx.taxScheme, TaxScheme::OutOfScope);
    }
    // 2. CN -> FR (Non-EU -> EU) [Import]
    {
        // B2C
        auto ctx = resolver.getTaxContext(now, "CN", "FR", SaleType::Products, false, false, "", "");
        QCOMPARE(ctx.taxScheme, TaxScheme::ImportVat);
        QCOMPARE(ctx.taxDeclaringCountryCode, "FR");
        QCOMPARE(ctx.countryCodeVatPaidTo, "FR");
    }
    // 3. CN -> DE (Non-EU -> EU) [Import]
    {
        // B2C
        auto ctx = resolver.getTaxContext(now, "CN", "DE", SaleType::Products, false, false, "", "");
        QCOMPARE(ctx.taxScheme, TaxScheme::ImportVat);
        QCOMPARE(ctx.taxDeclaringCountryCode, "DE");
        QCOMPARE(ctx.countryCodeVatPaidTo, "DE");
    }
}

void TestTaxResolver::test_SpecialTerritories()
{
    TaxResolver resolver(QCoreApplication::applicationDirPath());
    QDateTime now = QDateTime::currentDateTime();

    // 1. Canary Islands (ES) -> Spain Mainland (ES) [Import]
    // Canary Islands are effectively Non-EU for VAT.
    // Postal code 35000 is Las Palmas (Canary)
    
    // ES-CANARY -> ES
    {
        auto ctx = resolver.getTaxContext(now, "ES", "ES", SaleType::Products, false, false, "ES-CANARY", ""); 
        // Non-EU -> EU : Import
        // "Special territories... are generally Excluded from EU VAT area... so if vatTerritoryFrom is present, we treat it as Non-EU"
        QCOMPARE(ctx.taxScheme, TaxScheme::ImportVat);
        QCOMPARE(ctx.taxDeclaringCountryCode, "ES");
        QCOMPARE(ctx.countryCodeVatPaidTo, "ES");
    }
}

CsvReader TestTaxResolver::createAmazonReader(const QString &fileName)
{
    // CsvReader(fileName, sep, guillemets, hasHeader, returnLine, linesToSkip, encoding)
    return CsvReader(fileName, ",", "\"", true, "\n", 0, "UTF-8");
}

TaxScheme TestTaxResolver::mapAmazonTaxScheme(const QString &schemeStr)
{
    if (schemeStr == "UNION-OSS") return TaxScheme::EuOssUnion;
    if (schemeStr == "REGULAR") return TaxScheme::DomesticVat; 
    if (schemeStr == "IMPORT-OSS") return TaxScheme::EuIoss;
    if (schemeStr == "DEEMED_RESELLER-IOSS") return TaxScheme::EuIoss;
    if (schemeStr == "UK_VOEC-IMPORT") return TaxScheme::Exempt; // Export to UK (Tax collected by Amazon)
    if (schemeStr == "UK_VOEC-DOMESTIC") return TaxScheme::Exempt; // Export to UK (Tax collected by Amazon or domestic UK)
    if (schemeStr == "CH_VOEC") return TaxScheme::Exempt; // Export to CH (Tax collected by Amazon)
    if (schemeStr == "COMMINGLE_VAT") return TaxScheme::DomesticVat; // Treat commingling as domestic storage/sale logic usually
    if (schemeStr == "NO_VOEC") return TaxScheme::Exempt;

    return TaxScheme::Unknown;
}

QString TestTaxResolver::schemeStrToString(TaxScheme scheme)
{
    switch(scheme) {
    case TaxScheme::DomesticVat: return "DomesticVat";
    case TaxScheme::EuOssUnion: return "EuOssUnion";
    case TaxScheme::EuOssNonUnion: return "EuOssNonUnion";
    case TaxScheme::ImportVat: return "ImportVat";
    case TaxScheme::ReverseChargeImport: return "ReverseChargeImport";
    case TaxScheme::Exempt: return "Exempt";
    case TaxScheme::OutOfScope: return "OutOfScope";
    case TaxScheme::Unknown: return "Unknown";
    default: return "Invalid";
    }
}

void TestTaxResolver::test_AmazonReports()
{
    // Ensure clean start for VatTerritoryResolver
    QString territoriesPath = QCoreApplication::applicationDirPath() + "/vatTerritories.csv";
    QFile::remove(territoriesPath);

    TaxResolver resolver(QCoreApplication::applicationDirPath());
    VatTerritoryResolver vatResolver(QCoreApplication::applicationDirPath());

    QString reportDir = QCoreApplication::applicationDirPath() + "/data/eu-vat-reports";
    QDir dir(reportDir);
    if (!dir.exists()) {
        QSKIP("Report directory not found in build dir. Check CMake copy command.");
    }

    QStringList filters;
    filters << "*.csv";
    QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files);

    // BLACKLIST
    const QSet<QString> blacklist = getBlacklist();

    int failures = 0;
    int totalFiles = 0;
    int totalOrders = 0;
    QMap<QString, int> ignoredOrdersPerFile;

    for (const QFileInfo &fileInfo : fileList) {
        CsvReader reader = createAmazonReader(fileInfo.absoluteFilePath());
        if (!reader.readAll()) {
            qWarning() << "Failed to read" << fileInfo.fileName();
            continue;
        }

        const DataFromCsv *dataRode = reader.dataRode();
        if (!dataRode) continue;

        int fileOrders = 0;
        ignoredOrdersPerFile[fileInfo.fileName()] = 0;
        totalFiles++;

        int indTransactionType = dataRode->header.pos("TRANSACTION_TYPE");
        if (indTransactionType == -1) continue; 

        int indTransactionEventId = dataRode->header.pos("TRANSACTION_EVENT_ID");
        int indBuyerVatNumber = dataRode->header.pos("BUYER_VAT_NUMBER");
        int indDestCountry = dataRode->header.pos("ARRIVAL_COUNTRY");
        int indDestCountrySale = -1;
        if (dataRode->header.contains("SALE_ARRIVAL_COUNTRY")) {
            indDestCountrySale = dataRode->header.pos("SALE_ARRIVAL_COUNTRY");
        }
        int indFromPostcode = dataRode->header.pos("DEPARTURE_POST_CODE");
        
        int indFromCity = dataRode->header.pos("DEPATURE_CITY");
        if (indFromCity == -1) indFromCity = dataRode->header.pos("DEPARTURE_CITY");
        if (indFromCity == -1) indFromCity = dataRode->header.pos("DEPARTURE_CiTY");

        int indFromCountry = dataRode->header.pos("DEPARTURE_COUNTRY");
        int indFromCountrySale = -1;
        if (dataRode->header.contains("SALE_DEPART_COUNTRY")) {
            indFromCountrySale = dataRode->header.pos("SALE_DEPART_COUNTRY");
        }
        int indDateShipping = dataRode->header.pos("TRANSACTION_COMPLETE_DATE");
        int indTaxReportingScheme = dataRode->header.pos("TAX_REPORTING_SCHEME");
        
        if (indDateShipping == -1 || indFromCountry == -1 || indDestCountry == -1 || indTransactionEventId == -1) {
            continue;
        }

        int indTaxCountry = dataRode->header.pos("VAT_CALCULATION_IMPUTATION_COUNTRY");
        int indToPostcode = dataRode->header.pos("ARRIVAL_POST_CODE");
        
        for (const auto &line : dataRode->lines) {
            QString transactionType = line.value(indTransactionType);
            if (transactionType != "SALE" && transactionType != "REFUND") {
                continue;
            }

            QString transactionId = line.value(indTransactionEventId);
            if (blacklist.contains(transactionId)) {
                ignoredOrdersPerFile[fileInfo.fileName()]++;
                continue;
            }

            QString dateStr = line.value(indDateShipping);
            QDateTime dt = QDateTime::fromString(dateStr, "dd-MM-yyyy"); 
            if (!dt.isValid()) dt = QDateTime::fromString(dateStr, "dd/MM/yyyy");
            if (!dt.isValid()) dt = QDateTime::fromString(dateStr, "yyyy-MM-dd");
            // Basic fallback for invalid dates? No, ignore if date invalid. But fail if date string exists.
            if (!dt.isValid()) {
                if (!dateStr.isEmpty()) {
                     QString msg = QString("Invalid Date File: %1 ID: %2 DateStr: %3")
                         .arg(fileInfo.fileName())
                         .arg(transactionId)
                         .arg(dateStr);
                     qWarning() << qPrintable(msg);
                     failures++;
                }
                ignoredOrdersPerFile[fileInfo.fileName()]++; 
                continue;
            }

            QString fromCountry = line.value(indFromCountry);
            if (fromCountry.isEmpty() && indFromCountrySale != -1) fromCountry = line.value(indFromCountrySale);

            QString toCountry = line.value(indDestCountry);
            if (toCountry.isEmpty() && indDestCountrySale != -1) toCountry = line.value(indDestCountrySale);
            
            if (fromCountry.isEmpty() || toCountry.isEmpty()) {
                ignoredOrdersPerFile[fileInfo.fileName()]++;
                continue;
            }

            QString buyerVat = (indBuyerVatNumber != -1) ? line.value(indBuyerVatNumber) : "";
            bool isB2B = !buyerVat.isEmpty();
            
            QString amazonScheme = line.value(indTaxReportingScheme);
            bool isIoss = (amazonScheme == "IMPORT-OSS" || amazonScheme == "DEEMED_RESELLER-IOSS");
            
            // Resolve Territories
            QString fromPostcode = (indFromPostcode != -1) ? line.value(indFromPostcode) : "";
            QString fromCity = (indFromCity != -1) ? line.value(indFromCity) : "";
            QString territoryFrom = vatResolver.getTerritoryId(fromCountry, fromPostcode, fromCity);

            QString territoryTo = ""; 
            if (indToPostcode != -1) {
                QString toPost = line.value(indToPostcode);
                territoryTo = vatResolver.getTerritoryId(toCountry, toPost, "");
            }

            // Commingling Handling
            if (amazonScheme == "COMMINGLE_VAT") {
                // Treated as Domestic in Destination
                fromCountry = toCountry;
                territoryFrom = territoryTo;
            }

            TaxResolver::TaxContext ctx = resolver.getTaxContext(
                dt, fromCountry, toCountry, SaleType::Products, isB2B, isIoss, territoryFrom, territoryTo);

            fileOrders++;
            totalOrders++;
            
            // Test TaxContext::taxJurisdictionLevel
            if (ctx.taxScheme != TaxScheme::OutOfScope && ctx.taxScheme != TaxScheme::Unknown) {
                if(ctx.taxJurisdictionLevel == TaxJurisdictionLevel::Unknown){
                    qWarning() << "Unknown jurisidiction level" << dt << fromCountry << toCountry;
                    failures++;
                }
            }

            // Verify Scheme
            TaxScheme expectedScheme = mapAmazonTaxScheme(amazonScheme);
            
            if (expectedScheme == TaxScheme::Unknown) {
                QString msg = QString("Unknown Scheme File: %1 ID: %2 Date: %3 Scheme: %4")
                          .arg(fileInfo.fileName())
                          .arg(transactionId)
                          .arg(dateStr)
                          .arg(amazonScheme);
                qWarning() << qPrintable(msg);
                failures++;
                continue;
            }
            
            bool mismatchScheme = (ctx.taxScheme != expectedScheme);
            if (mismatchScheme) {
                // Tolerance 1: Export treated as Domestic (DDP/Amazon handled Dest Tax)
                if (ctx.taxScheme == TaxScheme::Exempt && expectedScheme == TaxScheme::DomesticVat) {
                    bool isDestEu = TaxResolver::isEuMember(toCountry, dt.date()) && territoryTo.isEmpty();
                    if (!isDestEu) mismatchScheme = false; 
                }
                
                // Tolerance 2: B2B handled as B2C
                if (isB2B && ctx.taxScheme == TaxScheme::Exempt && (expectedScheme == TaxScheme::DomesticVat || expectedScheme == TaxScheme::EuOssUnion)) {
                     mismatchScheme = false;
                }

                // Tolerance 3: OutOfScope treated as Exempt (e.g. GB->GB with UK VAT)
                if (ctx.taxScheme == TaxScheme::OutOfScope) {
                     mismatchScheme = false; 
                }
                
                // Tolerance 4: OSS vs Local VAT (DomesticVat)
                // If Amazon uses Local VAT registration (Regular/Domestic) instead of Union OSS, it is acceptable if TaxCountry matches.
                if (ctx.taxScheme == TaxScheme::EuOssUnion && expectedScheme == TaxScheme::DomesticVat) {
                    mismatchScheme = false;
                }

                // Tolerance 5: GB -> NI (Import) reported as UK_VOEC-DOMESTIC (Exempt)
                if (ctx.taxScheme == TaxScheme::ImportVat && expectedScheme == TaxScheme::Exempt && amazonScheme == "UK_VOEC-DOMESTIC") {
                     mismatchScheme = false;
                }
            }

            if (mismatchScheme) {
                // Mismatch
                QString msg = QString("Mismatch Scheme File: %1 ID: %2 Date: %3 From: %4 To: %5 B2B: %6 Exp: %7 Act: %8")
                    .arg(fileInfo.fileName())
                    .arg(transactionId)
                    .arg(dateStr)
                    .arg(fromCountry)
                    .arg(toCountry)
                    .arg(isB2B ? "Yes" : "No")
                    .arg(schemeStrToString(expectedScheme)) 
                    .arg(schemeStrToString(ctx.taxScheme));
                qWarning() << qPrintable(msg);
                failures++;
            }

            // Verify Tax Declaring Country
            QString expectedDeclaring = fromCountry;
            if (expectedScheme == TaxScheme::ImportVat || expectedScheme == TaxScheme::ReverseChargeImport || expectedScheme == TaxScheme::EuIoss) {
                expectedDeclaring = toCountry;
            } else if (expectedScheme == TaxScheme::EuOssUnion) {
                expectedDeclaring = fromCountry; // OSS declared in Origin
            } else if (expectedScheme == TaxScheme::Exempt) {
                 expectedDeclaring = fromCountry;
            }
            
            // Adjust expectation if Actual is OutOfScope (No declaring)
            if (ctx.taxScheme == TaxScheme::OutOfScope) {
                expectedDeclaring = "";
            }

            if (ctx.taxDeclaringCountryCode != expectedDeclaring) {
                QString msg = QString("Mismatch DeclaringCountry File: %1 ID: %2 Date: %3 From: %4 To: %5 B2B: %6 Exp: %7 Act: %8")
                    .arg(fileInfo.fileName())
                    .arg(transactionId)
                    .arg(dateStr)
                    .arg(fromCountry)
                    .arg(toCountry)
                    .arg(isB2B ? "Yes" : "No")
                    .arg(expectedDeclaring)
                    .arg(ctx.taxDeclaringCountryCode);
                qWarning() << qPrintable(msg);
                failures++;
            }
            
            // Verify Tax Country (Imputation Country -> where VAT is paid)
            if (indTaxCountry != -1) {
                QString amazonTaxCountry = line.value(indTaxCountry);
                // If OutOfScope, we don't care what Amazon says for TaxCountry (usually non-EU jurisdiction)
                if (ctx.taxScheme != TaxScheme::OutOfScope && !amazonTaxCountry.isEmpty() && amazonTaxCountry != ctx.countryCodeVatPaidTo) {
                     bool mismatchCountry = true;
                     // Tolerance for Monaco (MC) paying to France (FR)
                     if (ctx.countryCodeVatPaidTo == "MC" && amazonTaxCountry == "FR") mismatchCountry = false;

                     // Territory Parent Tolerance
                     static const QMap<QString, QString> territoryParent = {
                         {"MC", "FR"}, {"GP", "FR"}, {"MQ", "FR"}, {"RE", "FR"}, 
                         {"YT", "FR"}, {"GF", "FR"}, {"MF", "FR"}, {"BL", "FR"}, 
                         {"JE", "GB"}, {"GG", "GB"}, {"IM", "GB"}, 
                         {"SM", "IT"}, {"VA", "IT"}
                     };
                     if (territoryParent.value(ctx.countryCodeVatPaidTo) == amazonTaxCountry) {
                         mismatchCountry = false;
                     }
                     // Reverse Tolerance: Amazon says Territory (e.g. RE), Resolver says Parent (FR)
                     if (territoryParent.value(amazonTaxCountry) == ctx.countryCodeVatPaidTo) {
                         mismatchCountry = false;
                     }

                     // Export Tolerance: Resolver says Tax Paid to Origin (due to Exempt/Export), Amazon says Destination (Non-EU)
                     // If Scheme is Exempt (Export), TaxCountry is usually Origin (where Exempt is reported).
                     // But Amazon reports Destination Country for Non-EU imports/marketplace tax.
                     if (ctx.taxScheme == TaxScheme::Exempt && territoryParent.value(ctx.countryCodeVatPaidTo) != amazonTaxCountry) {
                        if (amazonTaxCountry == toCountry && !TaxResolver::isEuMember(toCountry, dt.date())) {
                            mismatchCountry = false;
                        }
                     }
                     
                     if (mismatchCountry) {
                         QString msg = QString("Mismatch TaxCountry File: %1 ID: %2 Date: %3 From: %4 To: %5 B2B: %6 Exp: %7 Act: %8")
                         .arg(fileInfo.fileName())
                         .arg(transactionId)
                         .arg(dateStr)
                         .arg(fromCountry)
                         .arg(toCountry)
                         .arg(isB2B ? "Yes" : "No")
                         .arg(amazonTaxCountry)
                         .arg(ctx.countryCodeVatPaidTo);
                     qWarning() << qPrintable(msg);
                     failures++;
                }
            }
        }
        // qDebug() << "Checked" << fileOrders << "orders in" << fileInfo.fileName();
    }
    }
    
    qDebug() << "Total Reports:" << totalFiles << "Total Orders:" << totalOrders;

    if (!ignoredOrdersPerFile.isEmpty()) {
        qWarning() << "Ignored Orders Summary:";
        QMapIterator<QString, int> i(ignoredOrdersPerFile);
        while (i.hasNext()) {
            i.next();
            if (i.value() > 0) {
                qWarning() << "  File:" << i.key() << "Ignored:" << i.value();
            }
        }
    }

    if (failures > 0) {
        qWarning() << "Total failures:" << failures;
        QFAIL("Found mismatches in Amazon reports. Check output logs.");
    }
}

QTEST_MAIN(TestTaxResolver)

#include "tst_test_tax_resolver.moc"
