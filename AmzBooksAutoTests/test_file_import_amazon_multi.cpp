// Tests the combined import of ImporterFileAmazonFbaInvoicing + ImporterFileAmazonVatEu
// into a shared OrderManager, verifying that every shipment and refund can be invoiced
// (i.e. getInvoicingInfo() never returns null — no "Missing invoicing info" errors).
//
// Regression: ImporterFileAmazonFbaInvoicing used to create Shipments without any
// InvoicingInfo, causing "Missing invoicing info for <orderId>" for every order that
// appeared only in the FBA invoicing report (non-EU exports, etc.).

#include <QtTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QCoroTask>

#include "orders/ImporterFileAmazonFbaInvoicing.h"
#include "orders/ImporterFileAmazonVatEu.h"
#include "orders/OrderManager.h"
#include "orders/InvoicingInfo.h"
#include "orders/Refund.h"

// ---------------------------------------------------------------------------
// CSV helpers
// ---------------------------------------------------------------------------

// Minimal FBA invoicing CSV row.
// Columns: Amazon Order Id, Shipment ID, Shipment Date, Currency,
//          Item Price, Item Tax, FC, Delivery Country Code, Sales Channel,
//          Recipient Name, Delivery Address 1, Delivery City/Town,
//          Delivery Postcode, Title, Merchant SKU, Dispatched Quantity,
//          Buyer E-mail
static const QString FBA_HEADER =
    "\"Amazon Order Id\",\"Shipment ID\",\"Shipment Date\",\"Currency\","
    "\"Item Price\",\"Item Tax\",\"FC\",\"Delivery Country Code\","
    "\"Sales Channel\",\"Recipient Name\",\"Delivery Address 1\","
    "\"Delivery City/Town\",\"Delivery Postcode\","
    "\"Title\",\"Merchant SKU\",\"Dispatched Quantity\",\"Buyer E-mail\"\n";

static QString fbaRow(const QString &orderId, const QString &shipId,
                      const QString &date, const QString &currency,
                      double price, double tax,
                      const QString &fc, const QString &destCountry,
                      const QString &channel,
                      const QString &recipientName,
                      const QString &title, const QString &sku, int qty,
                      const QString &email = QString())
{
    return QString("\"%1\",\"%2\",\"%3\",\"%4\","
                   "\"%5\",\"%6\",\"%7\",\"%8\","
                   "\"%9\",\"%10\",\"Rue Test\","
                   "\"TestCity\",\"12345\","
                   "\"%11\",\"%12\",\"%13\",\"%14\"\n")
        .arg(orderId, shipId, date, currency)
        .arg(price, 0, 'f', 2)
        .arg(tax, 0, 'f', 2)
        .arg(fc, destCountry, channel, recipientName)
        .arg(title, sku)
        .arg(qty)
        .arg(email);
}

// Minimal VAT EU CSV header — includes every column accessed via header.pos().
static const QString VAT_HEADER =
    "\"TRANSACTION_TYPE\","
    "\"TRANSACTION_EVENT_ID\","
    "\"ACTIVITY_TRANSACTION_ID\","
    "\"TAX_CALCULATION_DATE\","
    "\"TRANSACTION_COMPLETE_DATE\","
    "\"TOTAL_ACTIVITY_VALUE_AMT_VAT_EXCL\","
    "\"TOTAL_ACTIVITY_VALUE_VAT_AMT\","
    "\"TOTAL_ACTIVITY_VALUE_AMT_VAT_INCL\","
    "\"TRANSACTION_CURRENCY_CODE\","
    "\"SALE_DEPART_COUNTRY\","
    "\"SALE_ARRIVAL_COUNTRY\","
    "\"VAT_CALCULATION_IMPUTATION_COUNTRY\","
    "\"TAX_REPORTING_SCHEME\","
    "\"TAX_COLLECTION_RESPONSIBILITY\","
    "\"SELLER_SKU\","
    "\"ITEM_DESCRIPTION\","
    "\"QTY\","
    "\"PRICE_OF_ITEMS_VAT_RATE_PERCENT\","
    "\"TOTAL_ACTIVITY_VALUE_AMT_VAT_EXCL\","
    "\"PRICE_OF_ITEMS_AMT_VAT_EXCL\","
    "\"VAT_INV_NUMBER\","
    "\"INVOICE_URL\","
    "\"MARKETPLACE\","
    "\"PRODUCT_TAX_CODE\"\n";

// Note: TOTAL_ACTIVITY_VALUE_AMT_VAT_EXCL appears twice in VAT_HEADER above
// because CsvHeader uses only the first occurrence when building its position
// map.  The second occurrence is just to satisfy the "required column" check
// which is separate from pos().  In practice the real file has it once; here
// we deduplicate by using the same column name in both the required check and
// the pos() call (both refer to position 5).
//
// Actually CsvHeader stores positions by name in a QHash, so duplicates would
// overwrite each other.  Let's use a clean minimal header instead:

static const QString VAT_HEADER_CLEAN =
    "\"TRANSACTION_TYPE\","
    "\"TRANSACTION_EVENT_ID\","
    "\"ACTIVITY_TRANSACTION_ID\","
    "\"TAX_CALCULATION_DATE\","
    "\"TRANSACTION_COMPLETE_DATE\","
    "\"TOTAL_ACTIVITY_VALUE_AMT_VAT_EXCL\","
    "\"TOTAL_ACTIVITY_VALUE_VAT_AMT\","
    "\"TRANSACTION_CURRENCY_CODE\","
    "\"SALE_DEPART_COUNTRY\","
    "\"SALE_ARRIVAL_COUNTRY\","
    "\"VAT_CALCULATION_IMPUTATION_COUNTRY\","
    "\"TAX_REPORTING_SCHEME\","
    "\"TAX_COLLECTION_RESPONSIBILITY\","
    "\"SELLER_SKU\","
    "\"ITEM_DESCRIPTION\","
    "\"QTY\","
    "\"PRICE_OF_ITEMS_VAT_RATE_PERCENT\","
    "\"PRICE_OF_ITEMS_AMT_VAT_EXCL\","
    "\"VAT_INV_NUMBER\","
    "\"INVOICE_URL\","
    "\"MARKETPLACE\","
    "\"PRODUCT_TAX_CODE\"\n";

// Build one VAT EU CSV row.
// amtExcl / vatAmt are signed (negative for refunds).
static QString vatRow(const QString &type,
                      const QString &orderId, const QString &actId,
                      const QString &date,
                      double amtExcl, double vatAmt,
                      const QString &currency,
                      const QString &departCountry, const QString &arrivalCountry,
                      const QString &vatCountry,
                      const QString &scheme,
                      const QString &sku = QString(),
                      const QString &desc = QString(),
                      int qty = 1,
                      double vatRatePct = 0.0,
                      const QString &invNumber = QString(),
                      const QString &marketplace = "amazon.fr")
{
    return QString("\"%1\",\"%2\",\"%3\","
                   "\"%4\",\"%5\","
                   "\"%6\",\"%7\",\"%8\","
                   "\"%9\",\"%10\","
                   "\"%11\",\"%12\","
                   "\"SELLER\","       // TAX_COLLECTION_RESPONSIBILITY
                   "\"%13\",\"%14\",\"%15\","
                   "\"%16\",\"%17\","  // VAT rate, PRICE_OF_ITEMS_AMT_VAT_EXCL
                   "\"%18\",\"\","     // VAT_INV_NUMBER, INVOICE_URL
                   "\"%19\","          // MARKETPLACE
                   "\"A_GEN_STANDARD\"\n") // PRODUCT_TAX_CODE
        .arg(type, orderId, actId)
        .arg(date, date)                     // TAX_CALCULATION_DATE, TRANSACTION_COMPLETE_DATE
        .arg(amtExcl, 0, 'f', 2)
        .arg(vatAmt, 0, 'f', 2)
        .arg(currency, departCountry, arrivalCountry, vatCountry, scheme)
        .arg(sku, desc)
        .arg(qty)
        .arg(vatRatePct, 0, 'f', 2)
        .arg(amtExcl, 0, 'f', 2)     // PRICE_OF_ITEMS_AMT_VAT_EXCL (same as total excl)
        .arg(invNumber, marketplace);
}

// ---------------------------------------------------------------------------

class TestFileImportAmazonMulti : public QObject
{
    Q_OBJECT

private slots:
    // Regression: FBA-only shipments (no VAT EU counterpart) must have
    // InvoicingInfo so that invoice generation does not fail.
    void test_fbaOnly_hasInvoicingInfo();

    // Verify that after importing both FBA invoicing + VAT EU (with a refund),
    // every shipment and refund stored in OrderManager has InvoicingInfo.
    void test_multiImport_allShipmentsHaveInvoicingInfo();

    // Regression: if VAT EU is imported first (storing an invoice number) and
    // FBA invoicing is imported second (items only, no invoice number), the
    // invoice number must NOT be erased by the second recordInvoicingInfo call.
    void test_vatFirst_fbaSecond_preservesInvoiceNumber();
};

// ---------------------------------------------------------------------------
// test_fbaOnly_hasInvoicingInfo
// ---------------------------------------------------------------------------
// Before the fix, ImporterFileAmazonFbaInvoicing produced zero InvoicingInfos,
// so orders that only appeared in the FBA report triggered "Missing invoicing
// info for <orderId>" during invoice generation.
void TestFileImportAmazonMulti::test_fbaOnly_hasInvoicingInfo()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    // Write FBA invoicing CSV — two separate orders, each with a Title/SKU.
    const QString fbaFile = tmpDir.filePath("fba.csv");
    {
        QFile f(fbaFile);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << FBA_HEADER;
        // Order A: DE→CH (non-EU, 0% VAT — would not appear in the EU VAT report)
        out << fbaRow("302-1111111-1111111", "SHIP_A",
                      "2025-03-01T10:00:00+00:00", "EUR",
                      10.00, 0.00, "LEJ1", "CH",
                      "amazon.de", "Hans Muster",
                      "Test Widget A", "WDG-A001", 1, "buyer@example.com");
        // Order B: FR→BE (EU OSS — also present in VAT EU report in the next test)
        out << fbaRow("302-2222222-2222222", "SHIP_B",
                      "2025-03-02T10:00:00+00:00", "EUR",
                      20.00, 4.00, "LYS4", "BE",
                      "amazon.fr", "Marie Dupont",
                      "Test Gadget B", "GDG-B002", 2, "buyer2@example.com");
    }

    ImporterFileAmazonFbaInvoicing importer(tmpDir.path());
    AbstractImporter::ReturnOrderInfos result;
    try {
        result = QCoro::waitFor(importer.loadReport(fbaFile));
    } catch (const std::exception &e) {
        QFAIL(e.what());
    }

    QVERIFY2(result.errorReturned.isEmpty(), qPrintable(result.errorReturned));
    QVERIFY(result.orderInfos);
    QCOMPARE(result.orderInfos->shipments.size(), 2);

    // REGRESSION CHECK: before the fix this was 0; after the fix it must equal
    // the number of shipments that have a non-empty title.
    QCOMPARE(result.orderInfos->invoicingInfos.size(), 2);

    // Verify line-item content for Order A (no VAT)
    bool foundA = false;
    bool foundB = false;
    for (const auto &inv : std::as_const(result.orderInfos->invoicingInfos)) {
        QVERIFY(!inv.invoicingInfo.getItems().isEmpty());
        const auto &item = inv.invoicingInfo.getItems().first();
        if (inv.shipmentOrRefundId == "SHIP_A") {
            foundA = true;
            QVERIFY(qAbs(item.getTotalTaxed() - 10.00) < 0.01);
            QVERIFY(qAbs(item.getTotalTaxes() - 0.00) < 0.01);
            QCOMPARE(item.getSku(), QString("WDG-A001"));
        }
        if (inv.shipmentOrRefundId == "SHIP_B") {
            foundB = true;
            // qty=2 → total taxed = 2 * (20+4)/2 * 2 = 24 EUR total
            QVERIFY(qAbs(item.getTotalTaxed() - 24.00) < 0.01);
            QCOMPARE(item.getSku(), QString("GDG-B002"));
        }
    }
    QVERIFY(foundA);
    QVERIFY(foundB);
}

// ---------------------------------------------------------------------------
// test_multiImport_allShipmentsHaveInvoicingInfo
// ---------------------------------------------------------------------------
// Simulates PaneOrderFiles importing FBA invoicing then VAT EU (with a refund)
// into the same OrderManager, then verifies that getInvoicingInfo() never
// returns null for any shipment or refund in the "needs invoice" list.
void TestFileImportAmazonMulti::test_multiImport_allShipmentsHaveInvoicingInfo()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    // -----------------------------------------------------------------------
    // Order A: appears ONLY in the FBA invoicing report (e.g. export to CH).
    // Order B: appears in BOTH reports, and the VAT EU report has a refund too.
    // -----------------------------------------------------------------------

    // Write FBA invoicing CSV
    const QString fbaFile = tmpDir.filePath("fba.csv");
    {
        QFile f(fbaFile);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << FBA_HEADER;
        out << fbaRow("302-1111111-1111111", "SHIP_A",
                      "2025-03-01T10:00:00+00:00", "EUR",
                      10.00, 0.00, "LEJ1", "CH",
                      "amazon.de", "Hans Muster",
                      "Test Widget A", "WDG-A001", 1, "buyer@example.com");
        out << fbaRow("302-2222222-2222222", "SHIP_B",
                      "2025-03-02T10:00:00+00:00", "EUR",
                      16.53, 3.31, "LYS4", "BE",
                      "amazon.fr", "Marie Dupont",
                      "Test Gadget B", "GDG-B002", 1, "buyer2@example.com");
    }

    // Write VAT EU CSV — SALE + REFUND for Order B only
    const QString vatFile = tmpDir.filePath("vat-eu.csv");
    {
        QFile f(vatFile);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << VAT_HEADER_CLEAN;
        // SALE for order B
        out << vatRow("SALE",
                      "302-2222222-2222222", "ACT_B_SALE",
                      "02-03-2025",
                      16.53, 3.31, "EUR", "FR", "BE", "BE",
                      "UNION-OSS",
                      "GDG-B002", "Test Gadget B", 1, 20.0,
                      "", "amazon.fr");
        // REFUND for order B
        out << vatRow("REFUND",
                      "302-2222222-2222222", "ACT_B_REFUND",
                      "10-03-2025",
                      -16.53, -3.31, "EUR", "FR", "BE", "BE",
                      "UNION-OSS",
                      "GDG-B002", "Test Gadget B", 1, 20.0,
                      "", "amazon.fr");
    }

    // -----------------------------------------------------------------------
    // Import FBA invoicing
    // -----------------------------------------------------------------------
    ImporterFileAmazonFbaInvoicing fbaImporter(tmpDir.path());
    AbstractImporter::ReturnOrderInfos fbaResult;
    try {
        fbaResult = QCoro::waitFor(fbaImporter.loadReport(fbaFile));
    } catch (const std::exception &e) {
        QFAIL(e.what());
    }
    QVERIFY2(fbaResult.errorReturned.isEmpty(), qPrintable(fbaResult.errorReturned));
    QVERIFY(fbaResult.orderInfos);

    // -----------------------------------------------------------------------
    // Import VAT EU
    // -----------------------------------------------------------------------
    ImporterFileAmazonVatEu vatImporter(tmpDir.path());
    AbstractImporter::ReturnOrderInfos vatResult;
    try {
        vatResult = QCoro::waitFor(vatImporter.loadReport(vatFile));
    } catch (const std::exception &e) {
        QFAIL(e.what());
    }
    QVERIFY2(vatResult.errorReturned.isEmpty(), qPrintable(vatResult.errorReturned));
    QVERIFY(vatResult.orderInfos);

    QCOMPARE(vatResult.orderInfos->shipments.size(), 1);  // 1 SALE
    QCOMPARE(vatResult.orderInfos->refunds.size(), 1);    // 1 REFUND
    QCOMPARE(vatResult.orderInfos->invoicingInfos.size(), 2); // SALE + REFUND

    // -----------------------------------------------------------------------
    // Store everything in OrderManager (mirrors PaneOrderFiles logic)
    // -----------------------------------------------------------------------
    OrderManager orderManager(tmpDir.path());

    {
        ActivitySource fbaSource = fbaImporter.getActivitySource();
        QList<OrderManager::ShipmentFromSourceEntry> entries;
        for (const auto &s : fbaResult.orderInfos->shipments) {
            entries.append({s.getActivities().first().getEventId(),
                            &s, QDate(), fbaImporter.isWrongIfConflict(), false});
        }
        orderManager.recordShipmentsFromSource(&fbaSource, entries);
    }
    for (const auto &inv : std::as_const(fbaResult.orderInfos->invoicingInfos)) {
        orderManager.recordInvoicingInfo(inv.shipmentOrRefundId, &inv.invoicingInfo);
    }

    {
        ActivitySource vatSource = vatImporter.getActivitySource();
        QList<OrderManager::ShipmentFromSourceEntry> entries;
        for (const auto &s : vatResult.orderInfos->shipments) {
            entries.append({s.getActivities().first().getEventId(),
                            &s, QDate(), vatImporter.isWrongIfConflict(), false});
        }
        for (const auto &r : vatResult.orderInfos->refunds) {
            entries.append({r.getActivities().first().getEventId(),
                            &r, QDate(), vatImporter.isWrongIfConflict(), vatImporter.fixRefundDate()});
        }
        orderManager.recordShipmentsFromSource(&vatSource, entries);
    }
    for (const auto &inv : std::as_const(vatResult.orderInfos->invoicingInfos)) {
        orderManager.recordInvoicingInfo(inv.shipmentOrRefundId, &inv.invoicingInfo);
    }

    // -----------------------------------------------------------------------
    // Verify: every shipment in the "needs invoice" list has InvoicingInfo.
    // This is the exact check that PaneBookKeeping::generateInvoices() does;
    // failing it produces "Missing invoicing info for <orderId>".
    // -----------------------------------------------------------------------
    const QDate from(2025, 1, 1);
    const QDate to(2025, 12, 31);
    auto noInvMap = orderManager.get_channel_site_ShipmentAndRefundsNoInvoices(from, to);
    QVERIFY(!noInvMap.isNull());

    // There must be something to invoice
    int totalEntries = 0;
    for (auto chanIt = noInvMap->cbegin(); chanIt != noInvMap->cend(); ++chanIt) {
        for (auto siteIt = chanIt->cbegin(); siteIt != chanIt->cend(); ++siteIt) {
            for (auto ctxIt = siteIt->cbegin(); ctxIt != siteIt->cend(); ++ctxIt) {
                const auto &srwa = ctxIt.value();
                for (int i = 0; i < srwa.shipmentsRefundsSameActivity.size(); ++i) {
                    if (!srwa.invoicesToDo.value(i, false)) {
                        continue;
                    }
                    ++totalEntries;
                    const auto &shipment = srwa.shipmentsRefundsSameActivity[i];
                    QVERIFY(shipment);
                    QVERIFY(!shipment->getActivities().isEmpty());

                    const QString activityId =
                        shipment->getActivities().first().getActivityId();
                    const QString orderId =
                        shipment->getActivities().first().getEventId();

                    // REGRESSION CHECK: this must never be null.
                    auto info = orderManager.getInvoicingInfo(activityId);
                    if (info.isNull()) {
                        // Also try the group-level fallback (same as PaneBookKeeping)
                        info = srwa.invoicingInfo;
                    }
                    QVERIFY2(!info.isNull(),
                             qPrintable(QString("Missing invoicing info for %1 (activityId=%2)")
                                            .arg(orderId, activityId)));
                }
            }
        }
    }

    // Sanity: we must have checked at least one entry (Order A + Order B sale + refund)
    QVERIFY(totalEntries >= 1);
}

// ---------------------------------------------------------------------------
// test_vatFirst_fbaSecond_preservesInvoiceNumber
// ---------------------------------------------------------------------------
// Regression for the real-world case where:
//   1. The VAT EU report is imported first  → invoicing_infos gets invoiceNumber.
//   2. The FBA invoicing report is imported after → recordInvoicingInfo() is called
//      with an InvoicingInfo that has items but NO invoiceNumber.
// Before the fix, step 2 used INSERT OR REPLACE and silently erased the invoice
// number stored in step 1, causing the order to reappear in "Orders Without
// Invoices" even though Amazon had already issued an invoice.
void TestFileImportAmazonMulti::test_vatFirst_fbaSecond_preservesInvoiceNumber()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    // Single order that appears in BOTH reports.
    const QString orderId  = "407-9999999-9999999";
    const QString shipId   = "SHIP_X";
    const QString invNum   = "FR600006TESTINV";
    const QString invUrl   = "https://sellercentral.amazon.fr/document/download?v=test";

    // -----------------------------------------------------------------------
    // Write VAT EU CSV (SALE with invoice number + URL)
    // -----------------------------------------------------------------------
    const QString vatFile = tmpDir.filePath("vat-eu.csv");
    {
        QFile f(vatFile);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        // Use VAT_HEADER_CLEAN with an extra INVOICE_URL column in the row
        // vatRow() puts invNumber at position 18 and URL is hard-coded empty;
        // we need a URL too, so we build the row manually here.
        out << VAT_HEADER_CLEAN;
        out << QString("\"%1\",\"%2\",\"%3\","
                       "\"%4\",\"%5\","
                       "\"%6\",\"%7\",\"%8\","
                       "\"%9\",\"%10\","
                       "\"%11\",\"%12\","
                       "\"SELLER\","
                       "\"%13\",\"%14\",\"%15\","
                       "\"%16\",\"%17\","
                       "\"%18\",\"%19\","
                       "\"%20\","
                       "\"A_GEN_STANDARD\"\n")
                .arg("SALE", orderId, shipId)
                .arg("02-01-2026", "02-01-2026")
                .arg(24.79, 0, 'f', 2)
                .arg(5.20,  0, 'f', 2)
                .arg("EUR", "FR", "NL", "NL", "UNION-OSS")
                .arg("SKU-X", "Test Product X")
                .arg(1)
                .arg(21.0, 0, 'f', 2)
                .arg(24.79, 0, 'f', 2)
                .arg(invNum, invUrl)
                .arg("amazon.nl");
    }

    // -----------------------------------------------------------------------
    // Write FBA invoicing CSV (same order, same shipment, NO invoice fields)
    // -----------------------------------------------------------------------
    const QString fbaFile = tmpDir.filePath("fba.csv");
    {
        QFile f(fbaFile);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << FBA_HEADER;
        out << fbaRow(orderId, shipId,
                      "2026-01-02T00:00:00+00:00", "EUR",
                      24.79, 5.20, "LYS4", "NL",
                      "amazon.nl", "Test Buyer",
                      "Test Product X", "SKU-X", 1, "buyer@example.com");
    }

    OrderManager orderManager(tmpDir.path());

    // -----------------------------------------------------------------------
    // Step 1: import VAT EU — stores invoiceNumber + invoiceLink
    // -----------------------------------------------------------------------
    ImporterFileAmazonVatEu vatImporter(tmpDir.path());
    AbstractImporter::ReturnOrderInfos vatResult;
    try {
        vatResult = QCoro::waitFor(vatImporter.loadReport(vatFile));
    } catch (const std::exception &e) {
        QFAIL(e.what());
    }
    QVERIFY2(vatResult.errorReturned.isEmpty(), qPrintable(vatResult.errorReturned));
    QVERIFY(vatResult.orderInfos);
    QCOMPARE(vatResult.orderInfos->invoicingInfos.size(), 1);

    {
        ActivitySource vatSource = vatImporter.getActivitySource();
        QList<OrderManager::ShipmentFromSourceEntry> entries;
        for (const auto &s : std::as_const(vatResult.orderInfos->shipments)) {
            entries.append({s.getActivities().first().getEventId(),
                            &s, QDate(), vatImporter.isWrongIfConflict(), false});
        }
        orderManager.recordShipmentsFromSource(&vatSource, entries);
    }
    for (const auto &inv : std::as_const(vatResult.orderInfos->invoicingInfos)) {
        orderManager.recordInvoicingInfo(inv.shipmentOrRefundId, &inv.invoicingInfo);
    }

    // Confirm invoice number was saved
    {
        auto info = orderManager.getInvoicingInfo(shipId);
        QVERIFY2(!info.isNull(), "invoicingInfo should exist after VAT EU import");
        QVERIFY2(info->getInvoiceNumber().has_value(), "invoiceNumber should be set after VAT EU import");
        QCOMPARE(info->getInvoiceNumber().value(), invNum);
    }

    // -----------------------------------------------------------------------
    // Step 2: import FBA invoicing — must NOT erase the invoice number
    // -----------------------------------------------------------------------
    ImporterFileAmazonFbaInvoicing fbaImporter(tmpDir.path());
    AbstractImporter::ReturnOrderInfos fbaResult;
    try {
        fbaResult = QCoro::waitFor(fbaImporter.loadReport(fbaFile));
    } catch (const std::exception &e) {
        QFAIL(e.what());
    }
    QVERIFY2(fbaResult.errorReturned.isEmpty(), qPrintable(fbaResult.errorReturned));
    QVERIFY(fbaResult.orderInfos);

    {
        ActivitySource fbaSource = fbaImporter.getActivitySource();
        QList<OrderManager::ShipmentFromSourceEntry> entries;
        for (const auto &s : std::as_const(fbaResult.orderInfos->shipments)) {
            entries.append({s.getActivities().first().getEventId(),
                            &s, QDate(), fbaImporter.isWrongIfConflict(), false});
        }
        orderManager.recordShipmentsFromSource(&fbaSource, entries);
    }
    for (const auto &inv : std::as_const(fbaResult.orderInfos->invoicingInfos)) {
        orderManager.recordInvoicingInfo(inv.shipmentOrRefundId, &inv.invoicingInfo);
    }

    // -----------------------------------------------------------------------
    // Verify: invoice number and link must still be present after FBA import
    // -----------------------------------------------------------------------
    auto info = orderManager.getInvoicingInfo(shipId);
    QVERIFY2(!info.isNull(), "invoicingInfo must still exist after FBA import");
    QVERIFY2(info->getInvoiceNumber().has_value(),
             "invoiceNumber must NOT be erased by FBA invoicing re-import");
    QCOMPARE(info->getInvoiceNumber().value(), invNum);
    QVERIFY2(info->getInvoiceLink().has_value(),
             "invoiceLink must NOT be erased by FBA invoicing re-import");
    QCOMPARE(info->getInvoiceLink().value(), invUrl);
}

QTEST_MAIN(TestFileImportAmazonMulti)
#include "test_file_import_amazon_multi.moc"
