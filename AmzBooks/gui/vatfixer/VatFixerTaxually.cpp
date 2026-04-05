#include "VatFixerTaxually.h"
#include "gui/dialogs/DialogValidOrders.h"

#include "inventory/PurchaseCsvLoader.h"

#include <xlsxdocument.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMessageBox>
#include <QSet>
#include <QTextStream>
#include <algorithm>

// Self-register this fixer in AbstractVatFixer::ALL_FIXERS().
DECLARE_VAT_FIXER(VatFixerTaxually)

// ── Taxually-inventory xlsx sheet names ──────────────────────────────────────
static constexpr const char *SHEET_INVENTORY      = "Inventory";
static constexpr const char *SHEET_MISSING_SKU    = "Missing Sku list";
// Columns (1-based QXlsx): A=1 … G=7
static constexpr int COL_SKU      = 1; // A
static constexpr int COL_VAT_RATE = 5; // E
static constexpr int COL_PRICE    = 6; // F
static constexpr int COL_CURR     = 7; // G

// ── Name ──────────────────────────────────────────────────────────────────────

QString VatFixerTaxually::getName() const
{
    return QStringLiteral("Taxually");
}

// ── File-type detection ───────────────────────────────────────────────────────

bool VatFixerTaxually::isInventoryFile(const QString &filePath) const
{
    const QFileInfo fi(filePath);
    if (fi.suffix().compare("xlsx", Qt::CaseInsensitive) != 0) {
        return false;
    }
    // Must contain "inventory" in the name (case-insensitive).
    return fi.fileName().contains("inventory", Qt::CaseInsensitive);
}

QStringList VatFixerTaxually::getInventoryValidExtensions() const
{
    return {"xlsx"};
}

bool VatFixerTaxually::isOrderFile(const QString &filePath) const
{
    const QFileInfo fi(filePath);
    if (fi.suffix().compare("txt", Qt::CaseInsensitive) != 0) {
        return false;
    }
    // Taxually automated-download filenames contain "automated_download".
    return fi.fileName().contains("automated_download", Qt::CaseInsensitive);
}

QStringList VatFixerTaxually::getVatOrdersValidExtensions() const
{
    return {"txt"};
}

QStringList VatFixerTaxually::getVatSummaryValidExtensions() const
{
    return {"xlsx"};
}

// ── fixInventoryValue ─────────────────────────────────────────────────────────

AbstractVatFixer::InventoryFixResult VatFixerTaxually::fixInventoryValue(
    const QString     &filePath,
    const QString     &newFilePath,
    const QStringList &purchaseCsvFiles,
    const QDir        &settingsDir) const
{
    InventoryFixResult result;
    const QString xlsxBaseName = QFileInfo(filePath).fileName();

    // 1. Load prices from purchase CSV files (newest-first, first match wins).
    struct CsvPrice { double price; QString fileName; QString currency; };
    QHash<QString, CsvPrice> csvPrices; // SKU → price info
    if (!purchaseCsvFiles.isEmpty()) {
        const QList<PurchaseCsvLoader::Record> records =
            PurchaseCsvLoader::parseFiles(purchaseCsvFiles, settingsDir);
        for (const PurchaseCsvLoader::Record &rec : std::as_const(records)) {
            if (rec.unitPrice > 0.0 && !csvPrices.contains(rec.sku)) {
                csvPrices[rec.sku] = {rec.unitPrice, rec.fileName, rec.invoiceCurrency};
            }
        }
    }

    // 2. Open the taxually inventory xlsx.
    QXlsx::Document doc(filePath);
    if (!doc.isLoadPackage()) {
        return result;
    }

    const bool hasMissingSkuSheet = doc.sheetNames().contains(
        QLatin1String(SHEET_MISSING_SKU), Qt::CaseInsensitive);

    if (hasMissingSkuSheet) {
        // ── Type 1: standard inventory (Inventory + Missing Sku list sheets) ──
        // Read known prices from the "Inventory" sheet, then fill the
        // "Missing Sku list" sheet, trying the xlsx prices first and purchase
        // CSVs as fallback.

        QHash<QString, double> inventoryPrice;
        if (doc.selectSheet(SHEET_INVENTORY)) {
            const QXlsx::CellRange dim = doc.dimension();
            for (int row = 2; row <= dim.lastRow(); ++row) {
                const QVariant skuVar = doc.read(row, COL_SKU);
                if (skuVar.isNull()) {
                    continue;
                }
                const QString sku = skuVar.toString().trimmed();
                const QVariant priceVar = doc.read(row, COL_PRICE);
                if (!sku.isEmpty() && !priceVar.isNull() && priceVar.toDouble() > 0.0) {
                    inventoryPrice[sku] = priceVar.toDouble();
                }
            }
        }

        if (doc.selectSheet(SHEET_MISSING_SKU)) {
            const QXlsx::CellRange dim = doc.dimension();
            for (int row = 2; row <= dim.lastRow(); ++row) {
                const QVariant skuVar = doc.read(row, COL_SKU);
                if (skuVar.isNull()) {
                    continue;
                }
                const QString sku = skuVar.toString().trimmed();
                if (sku.isEmpty()) {
                    continue;
                }
                // Replace "Zero" VAT rate with "Standard".
                const QVariant vatRateVar = doc.read(row, COL_VAT_RATE);
                if (!vatRateVar.isNull() &&
                    vatRateVar.toString().trimmed().compare("Zero", Qt::CaseInsensitive) == 0) {
                    doc.write(row, COL_VAT_RATE, QString("Standard"));
                }
                const QVariant priceVar = doc.read(row, COL_PRICE);
                if (!priceVar.isNull() && priceVar.toDouble() > 0.0) {
                    continue; // price already set
                }

                // Try xlsx "Inventory" sheet first, then purchase CSVs.
                if (inventoryPrice.contains(sku)) {
                    const double price = inventoryPrice.value(sku);
                    doc.write(row, COL_PRICE, price);
                    doc.write(row, COL_CURR,  QStringLiteral("EUR"));
                    InventoryFixResult::FixedSku fixed;
                    fixed.sku        = sku;
                    fixed.price      = price;
                    fixed.currency   = QStringLiteral("EUR");
                    fixed.sourceFile = xlsxBaseName;
                    result.fixedSkus.append(fixed);
                } else if (csvPrices.contains(sku)) {
                    const CsvPrice &cp = csvPrices.value(sku);
                    const QString currency = cp.currency.isEmpty()
                                             ? QStringLiteral("EUR") : cp.currency;
                    doc.write(row, COL_PRICE, cp.price);
                    doc.write(row, COL_CURR,  currency);
                    InventoryFixResult::FixedSku fixed;
                    fixed.sku        = sku;
                    fixed.price      = cp.price;
                    fixed.currency   = currency;
                    fixed.sourceFile = cp.fileName;
                    result.fixedSkus.append(fixed);
                } else {
                    result.skusNotFound.append(sku);
                }
            }
        }
    } else {
        // ── Type 2: full-inventory (only Inventory sheet) ──────────────────────
        // For every SKU in the Inventory sheet, overwrite the price with the
        // most-recent value from purchase CSVs (prices may be stale in the
        // taxually-generated file).  SKUs absent from purchase CSVs that also
        // lack an existing price are reported in skusNotFound; those that
        // already carry a price are left unchanged and not reported.

        if (doc.selectSheet(SHEET_INVENTORY)) {
            const QXlsx::CellRange dim = doc.dimension();
            for (int row = 2; row <= dim.lastRow(); ++row) {
                const QVariant skuVar = doc.read(row, COL_SKU);
                if (skuVar.isNull()) {
                    continue;
                }
                const QString sku = skuVar.toString().trimmed();
                if (sku.isEmpty()) {
                    continue;
                }
                // Replace "Zero" VAT rate with "Standard".
                const QVariant vatRateVar = doc.read(row, COL_VAT_RATE);
                if (!vatRateVar.isNull() &&
                    vatRateVar.toString().trimmed().compare("Zero", Qt::CaseInsensitive) == 0) {
                    doc.write(row, COL_VAT_RATE, QString("Standard"));
                }

                if (csvPrices.contains(sku)) {
                    // Overwrite with the purchase-CSV price regardless of what
                    // the file currently contains.
                    const CsvPrice &cp = csvPrices.value(sku);
                    const QString currency = cp.currency.isEmpty()
                                             ? QStringLiteral("EUR") : cp.currency;
                    doc.write(row, COL_PRICE, cp.price);
                    doc.write(row, COL_CURR,  currency);
                    InventoryFixResult::FixedSku fixed;
                    fixed.sku        = sku;
                    fixed.price      = cp.price;
                    fixed.currency   = currency;
                    fixed.sourceFile = cp.fileName;
                    result.fixedSkus.append(fixed);
                } else {
                    // No purchase-CSV price available: only flag as missing when
                    // the cell is genuinely empty (has no fallback value at all).
                    const QVariant priceVar = doc.read(row, COL_PRICE);
                    if (priceVar.isNull() || priceVar.toDouble() <= 0.0) {
                        result.skusNotFound.append(sku);
                    }
                }
            }
        }
    }

    doc.saveAs(newFilePath);
    return result;
}

// ── fixVatOrders helpers ──────────────────────────────────────────────────────

// Read a date cell from QXlsx.
// QXlsx returns date cells as QDateTime/QDate inside the QVariant; calling
// .toDouble() on those gives 0.0, which maps to the Excel epoch 1899-12-30.
// Check the actual type first and fall back to serial-number conversion only
// for numeric variants.
static QDate xlsxDate(const QVariant &v)
{
    if (v.userType() == QMetaType::QDateTime) {
        return v.toDateTime().date();
    }
    if (v.userType() == QMetaType::QDate) {
        return v.toDate();
    }
    // Numeric Excel serial: epoch = 1899-12-30.
    const double serial = v.toDouble();
    if (serial < 1.0) {
        return {};
    }
    return QDate(1899, 12, 30).addDays(static_cast<int>(serial));
}

// Parse "EU|SA|20.00|G|DE|FR" → rate = 0.20
static double rateFromTaxCode(const QString &taxCode)
{
    const QStringList parts = taxCode.split('|');
    if (parts.size() < 3) {
        return 0.0;
    }
    bool ok = false;
    const double pct = parts[2].toDouble(&ok);
    return ok ? (pct / 100.0) : 0.0;
}

QList<VatOrderEntry> VatFixerTaxually::_parseReturnAnalytics(
    const QString &xlsxPath) const
{
    QXlsx::Document doc(xlsxPath);
    if (!doc.isLoadPackage()) {
        return {};
    }
    if (!doc.selectSheet("Tax return detail")) {
        return {};
    }

    const QXlsx::CellRange dim = doc.dimension();

    // Row 2: column headers. Find the columns we care about (B-S typically).
    // We do a dynamic lookup so layout changes don't break us.
    QHash<QString, int> hdrCol; // header text → 1-based column index
    for (int col = 1; col <= dim.lastColumn(); ++col) {
        const QVariant v = doc.read(2, col);
        if (!v.isNull() && !v.toString().trimmed().isEmpty()) {
            hdrCol[v.toString().trimmed()] = col;
        }
    }

    // Row 3: box-number labels.
    // ReturnAnalytics layout varies by country:
    //   DE:    pure numeric labels ("41 ", "81 ", …)  — all are NET amounts
    //   ES/IT: "(NET)" / "(VAT)" suffixed labels       — NET or direct VAT
    //
    // Strategy for computing taxuallyVat per row:
    //   • If any label in row 3 contains "(NET)" → use the FIRST non-null
    //     "(NET)" column value × rate.  (Multiple "(NET)" columns often repeat
    //     the same net into different VAT-return boxes, so we take only the first.)
    //   • Otherwise (DE: all numeric labels) → sum all non-null column values
    //     and multiply by rate.
    QList<int> netLabelCols;   // columns whose row-3 label contains "(NET)"
    QList<int> numericBoxCols; // columns whose row-3 label is a pure number (DE)
    for (int col = 1; col <= dim.lastColumn(); ++col) {
        const QVariant v = doc.read(3, col);
        if (v.isNull()) {
            continue;
        }
        const QString label = v.toString().trimmed();
        if (label.contains(QLatin1String("(NET)"), Qt::CaseInsensitive)) {
            netLabelCols.append(col);
        } else {
            bool ok = false;
            label.toDouble(&ok);
            if (ok) {
                numericBoxCols.append(col);
            }
        }
    }
    const bool useNetLabel = !netLabelCols.isEmpty();

    const int colB = hdrCol.value("Data source",     2);
    const int colD = hdrCol.value("Transaction ID",  4);
    const int colF = hdrCol.value("Transaction type",6);
    const int colG = hdrCol.value("Tax code",        7);
    const int colH = hdrCol.value("Transaction date",8);

    // Aggregate per (orderId, sourceFile, taxCode).
    struct Agg { double netForVat = 0.0; QString txType; QDate date; };
    QHash<QString, Agg> agg; // key = orderId + "|" + sourceFile + "|" + taxCode

    for (int row = 4; row <= dim.lastRow(); ++row) {
        const QVariant vOrderId = doc.read(row, colD);
        if (vOrderId.isNull()) {
            continue;
        }
        const QString orderId    = vOrderId.toString().trimmed();
        const QString sourceFile = doc.read(row, colB).toString().trimmed();
        const QString txType     = doc.read(row, colF).toString().trimmed();
        const QString taxCode    = doc.read(row, colG).toString().trimmed();
        const QDate   date       = xlsxDate(doc.read(row, colH));

        // Skip stock-movement rows — only customer-sale rows produce
        // discrepancies worth reviewing (movements are never in TXT files).
        if (!txType.contains(QLatin1String("sale"), Qt::CaseInsensitive) &&
            !txType.contains(QLatin1String("supply"), Qt::CaseInsensitive)) {
            continue;
        }

        // Skip OSS/IOSS entries (tax code starts with "EU|").
        // OSS distance sales are declared via the OSS scheme, not through the
        // domestic automated-download TXT files, so VAT=0 in TXT is correct.
        if (taxCode.startsWith(QLatin1String("EU|"), Qt::CaseInsensitive)) {
            continue;
        }

        double netForVat = 0.0;
        if (useNetLabel) {
            // ES/IT: first non-null "(NET)" column value
            for (const int bc : std::as_const(netLabelCols)) {
                const QVariant bv = doc.read(row, bc);
                if (!bv.isNull()) {
                    netForVat = bv.toDouble();
                    break;
                }
            }
        } else {
            // DE: sum all numeric-labeled box columns
            for (const int bc : std::as_const(numericBoxCols)) {
                const QVariant bv = doc.read(row, bc);
                if (!bv.isNull()) {
                    netForVat += bv.toDouble();
                }
            }
        }

        const QString aggKey = orderId + "|" + sourceFile + "|" + taxCode;
        auto &a = agg[aggKey];
        a.netForVat += netForVat;
        if (a.txType.isEmpty()) {
            a.txType = txType;
        }
        if (!a.date.isValid()) {
            a.date = date;
        }
    }

    QList<VatOrderEntry> result;
    for (auto it = agg.cbegin(); it != agg.cend(); ++it) {
        const QStringList parts = it.key().split('|');
        if (parts.size() < 3) {
            continue;
        }
        const QString orderId    = parts[0];
        const QString sourceFile = parts[1];
        const QString taxCode    = it.key().mid(orderId.size() + 1 + sourceFile.size() + 1);
        const double  rate       = rateFromTaxCode(taxCode);

        VatOrderEntry entry;
        entry.orderId         = orderId;
        entry.sourceFile      = sourceFile;
        entry.transactionType = it.value().txType;
        entry.date            = it.value().date;
        entry.taxCode         = taxCode;
        entry.taxRate         = rate;
        entry.netAmount       = it.value().netForVat;
        entry.taxuallyVat     = it.value().netForVat * rate;
        result.append(entry);
    }
    return result;
}

QHash<QString, VatFixerTaxually::TxtOrderInfo>
VatFixerTaxually::_parseTxtFile(const QString &txtPath) const
{
    QFile file(txtPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    // Header row
    const QString headerLine = in.readLine();
    const QStringList headers = headerLine.split('\t');

    auto colOf = [&](const QString &name) -> int {
        return headers.indexOf(name);
    };

    const int idxOrderId    = colOf("TRANSACTION_EVENT_ID");
    const int idxTxType     = colOf("TRANSACTION_TYPE");
    const int idxMarket     = colOf("MARKETPLACE");
    const int idxSku        = colOf("SELLER_SKU");
    const int idxDesc       = colOf("ITEM_DESCRIPTION");
    const int idxDeptCnt    = colOf("DEPARTURE_COUNTRY");
    const int idxArrCnt     = colOf("ARRIVAL_COUNTRY");
    const int idxTotalVat   = colOf("TOTAL_ACTIVITY_VALUE_VAT_AMT");

    QHash<QString, TxtOrderInfo> result;

    while (!in.atEnd()) {
        const QStringList fields = in.readLine().split('\t');
        if (fields.size() <= idxOrderId || idxOrderId < 0) {
            continue;
        }
        const QString orderId = fields.value(idxOrderId).trimmed();
        if (orderId.isEmpty()) {
            continue;
        }

        // Only accumulate VAT from SALE rows — REFUND/RETURN rows carry
        // negative VAT that would cancel the SALE VAT and hide real discrepancies.
        if (idxTxType >= 0) {
            const QString txType = fields.value(idxTxType).trimmed();
            if (!txType.isEmpty() && txType.compare("SALE", Qt::CaseInsensitive) != 0) {
                continue;
            }
        }

        bool ok = false;
        const double vat = fields.value(idxTotalVat).replace(',', '.').toDouble(&ok);

        auto &info = result[orderId];
        info.amazonVat += ok ? vat : 0.0;
        if (info.marketplace.isEmpty() && idxMarket >= 0) {
            info.marketplace = fields.value(idxMarket).trimmed();
        }
        if (info.sku.isEmpty() && idxSku >= 0) {
            info.sku = fields.value(idxSku).trimmed();
        }
        if (info.description.isEmpty() && idxDesc >= 0) {
            info.description = fields.value(idxDesc).trimmed();
        }
        if (info.departureCountry.isEmpty() && idxDeptCnt >= 0) {
            info.departureCountry = fields.value(idxDeptCnt).trimmed();
        }
        if (info.arrivalCountry.isEmpty() && idxArrCnt >= 0) {
            info.arrivalCountry = fields.value(idxArrCnt).trimmed();
        }
    }
    return result;
}

void VatFixerTaxually::writefixedTxt(const QString &txtPath,
                                       const QString &fixedPath,
                                       const QSet<QString> &removeIds,
                                       const QHash<QString, double> &fixRates) const
{
    QFile src(txtPath);
    if (!src.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    QFile dst(fixedPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    QTextStream in(&src);
    QTextStream out(&dst);
    in.setEncoding(QStringConverter::Utf8);
    out.setEncoding(QStringConverter::Utf8);

    // Header row — keep as-is, build column index map
    const QString headerLine = in.readLine();
    out << headerLine << "\n";
    const QStringList headers = headerLine.split('\t');

    auto colOf = [&](const QString &name) -> int {
        return headers.indexOf(name);
    };

    const int idxOrderId    = colOf("TRANSACTION_EVENT_ID");
    const int idxNetExcl    = colOf("TOTAL_ACTIVITY_VALUE_AMT_VAT_EXCL");  // col 30
    const int idxVatRate    = colOf("PRICE_OF_ITEMS_VAT_RATE_PERCENT");    // col 31
    const int idxItemVat    = colOf("PRICE_OF_ITEMS_VAT_AMT");             // col 32
    const int idxTotItemVat = colOf("TOTAL_PRICE_OF_ITEMS_VAT_AMT");       // col 34
    const int idxTotalVat   = colOf("TOTAL_ACTIVITY_VALUE_VAT_AMT");       // col 43
    const int idxItemIncl   = colOf("PRICE_OF_ITEMS_AMT_VAT_INCL");        // col 44
    const int idxTotItemIncl= colOf("TOTAL_PRICE_OF_ITEMS_AMT_VAT_INCL");  // col 46
    const int idxTotalIncl  = colOf("TOTAL_ACTIVITY_VALUE_AMT_VAT_INCL");  // col 53
    const int idxNetItem    = colOf("PRICE_OF_ITEMS_AMT_VAT_EXCL");        // col 21

    while (!in.atEnd()) {
        const QString line = in.readLine();
        QStringList fields = line.split('\t');

        const QString orderId = (idxOrderId >= 0 && idxOrderId < fields.size())
                                ? fields.value(idxOrderId).trimmed() : QString();

        if (removeIds.contains(orderId)) {
            continue; // drop this row
        }

        if (fixRates.contains(orderId)) {
            const double rate = fixRates.value(orderId);

            auto updateField = [&](int idx, double value) {
                if (idx >= 0 && idx < fields.size()) {
                    fields[idx] = QString::number(value, 'f', 2);
                }
            };

            bool okNet = false;
            const double netItem = (idxNetItem >= 0)
                ? fields.value(idxNetItem).replace(',', '.').toDouble(&okNet) : 0.0;
            const double netTotal = (idxNetExcl >= 0)
                ? fields.value(idxNetExcl).replace(',', '.').toDouble() : 0.0;

            const double itemVat  = okNet ? (netItem  * rate) : 0.0;
            const double totalVat = netTotal * rate;

            updateField(idxVatRate,     rate);
            updateField(idxItemVat,     itemVat);
            updateField(idxTotItemVat,  itemVat);
            updateField(idxTotalVat,    totalVat);
            updateField(idxItemIncl,    netItem  + itemVat);
            updateField(idxTotItemIncl, netItem  + itemVat);
            updateField(idxTotalIncl,   netTotal + totalVat);
        }

        out << fields.join('\t') << "\n";
    }
}

// ── findVatDiscrepancies / fixVatOrders ───────────────────────────────────────

QList<VatOrderEntry> VatFixerTaxually::findVatDiscrepancies(
    const QString     &vatFilePathSummary,
    const QStringList &vatFilePathToUpdate) const
{
    const QList<VatOrderEntry> allAnalyticsEntries =
        _parseReturnAnalytics(vatFilePathSummary);
    if (allAnalyticsEntries.isEmpty()) {
        return {};
    }

    QList<VatOrderEntry> discrepancies;

    for (const QString &txtPath : std::as_const(vatFilePathToUpdate)) {
        const QString txtBasename = QFileInfo(txtPath).fileName();

        // The Excel "Data source" column may omit a date prefix that the actual
        // file on disk carries (e.g. "2026-01-automated_download_foo.txt" on
        // disk vs "automated_download_foo.txt" in the Excel).  Accept both an
        // exact match and the case where the disk name ends with the Excel name.
        QList<VatOrderEntry> relevantEntries;
        for (const VatOrderEntry &ae : std::as_const(allAnalyticsEntries)) {
            if (ae.sourceFile == txtBasename || txtBasename.endsWith(ae.sourceFile)) {
                relevantEntries.append(ae);
            }
        }
        if (relevantEntries.isEmpty()) {
            continue;
        }

        const QHash<QString, TxtOrderInfo> txtData = _parseTxtFile(txtPath);

        for (VatOrderEntry entry : std::as_const(relevantEntries)) {
            // Only flag orders that actually appear in the TXT file.
            // If the order is absent from the TXT, it belongs to a different
            // channel or time window and is not a VAT discrepancy.
            if (!txtData.contains(entry.orderId)) {
                continue;
            }

            entry.sourceFile = txtPath; // store full path for later write

            const TxtOrderInfo &info = txtData.value(entry.orderId);
            entry.amazonVat        = info.amazonVat;
            entry.marketplace      = info.marketplace;
            entry.sku              = info.sku;
            entry.description      = info.description;
            entry.departureCountry = info.departureCountry;
            entry.arrivalCountry   = info.arrivalCountry;

            // Only flag orders where the TXT shows 0 VAT but the ReturnAnalytics
            // declares VAT > 0.10 — i.e. Taxually added VAT that is missing
            // from the automated-download file.
            if (qAbs(entry.amazonVat) < 0.01 && entry.taxuallyVat > 0.1) {
                // Default to Remove: these lines have 0 VAT in the TXT and
                // should be deleted rather than patched with a non-zero rate.
                entry.action = VatOrderEntry::Action::Remove;
                discrepancies.append(entry);
            }
        }
    }

    return discrepancies;
}

void VatFixerTaxually::fixVatOrders(const QStringList &vatFilePathsSummary,
                                     const QStringList &vatFilePathToUpdate,
                                     const QString &postFixBaseNameUpdated) const
{
    QList<VatOrderEntry> discrepancies;
    for (const QString &summaryPath : std::as_const(vatFilePathsSummary)) {
        discrepancies.append(findVatDiscrepancies(summaryPath, vatFilePathToUpdate));
    }

    if (discrepancies.isEmpty()) {
        QMessageBox::information(nullptr,
            QObject::tr("No discrepancies"),
            QObject::tr("All orders match within 0.10 €. Nothing to fix."));
        return;
    }

    // 3. Show the review dialog.
    DialogValidOrders dialog(discrepancies, nullptr);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QList<VatOrderEntry> approved = dialog.getApprovedEntries();
    if (approved.isEmpty()) {
        return;
    }

    // 4. Group approved entries by TXT file path, then apply fixes.
    QHash<QString, QSet<QString>>    removeByFile; // txtPath → orderIds to remove
    QHash<QString, QHash<QString, double>> fixByFile; // txtPath → orderId → rate

    for (const VatOrderEntry &entry : std::as_const(approved)) {
        if (entry.action == VatOrderEntry::Action::Remove) {
            removeByFile[entry.sourceFile].insert(entry.orderId);
        } else {
            fixByFile[entry.sourceFile][entry.orderId] = entry.taxRate;
        }
    }

    for (const QString &txtPath : std::as_const(vatFilePathToUpdate)) {
        if (!removeByFile.contains(txtPath) && !fixByFile.contains(txtPath)) {
            continue;
        }
        const QFileInfo fi(txtPath);
        const QString fixedPath = fi.absolutePath() + "/" +
                                  fi.completeBaseName() +
                                  postFixBaseNameUpdated + "." + fi.suffix();
        writefixedTxt(txtPath,
                      fixedPath,
                      removeByFile.value(txtPath),
                      fixByFile.value(txtPath));
    }
}
