#include "PurchaseCsvLoader.h"
#include "CurrencyRateManager.h"
#include "ExceptionWithTitleText.h"
#include "profit/PurchaseFileSettingsTree.h"
#include "utils/CsvReader.h"
#include <QFileInfo>

QList<PurchaseCsvLoader::Record> PurchaseCsvLoader::parseFiles(
        const QStringList &filePaths,
        const QDir &settingsDir,
        const QString &companyCurrency,
        const CurrencyRateManager *rateManager)
{
    PurchaseFileSettingsTree settingsTree(settingsDir);
    QList<Record> result;

    for (const QString &filePath : filePaths) {
        QFileInfo fi(filePath);
        const QString fileName = fi.fileName();

        // Parse and validate the mandatory YYYY-MM-DD__ date prefix.
        QDate fileDate;
        if (fileName.length() >= 10)
            fileDate = QDate::fromString(fileName.left(10), QStringLiteral("yyyy-MM-dd"));
        if (fileName.length() < 12 || !fileDate.isValid()
                || fileName[10] != QLatin1Char('_') || fileName[11] != QLatin1Char('_')) {
            throw ExceptionWithTitleText(
                    QStringLiteral("Invalid purchase file name"),
                    QStringLiteral("Purchase CSV file \"%1\" must start with a YYYY-MM-DD__ date prefix.")
                            .arg(fileName));
        }

        // Title priority from language suffix (case-insensitive).
        int titlePriority = 1;
        if (fileName.contains(QLatin1String("-FR"), Qt::CaseInsensitive)) {
            titlePriority = 3;
        } else if (fileName.contains(QLatin1String("-US"),  Qt::CaseInsensitive)
                   || fileName.contains(QLatin1String("-COM"), Qt::CaseInsensitive)
                   || fileName.contains(QLatin1String("-CA"),  Qt::CaseInsensitive)) {
            titlePriority = 2;
        }

        auto seps = CsvReader::guessColStringSeps(filePath);
        CsvReader reader(filePath, seps.first, seps.second, true, "\n", 0, "Latin1");
        if (!reader.readAll())
            continue;

        const DataFromCsv *rode = reader.dataRode();
        QStringList headers = rode->header.getHeaderElements();
        for (QString &h : headers)
            h = h.trimmed();

        int colSku      = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_SKU);
        int colTitle    = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_TITLE);
        int colPrice    = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_UNIT_PRICE);
        int colCurrency = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_CURRENCY);
        int colWeight   = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_UNIT_WEIGHT);
        int colQty      = settingsTree.getColPos(headers, PurchaseFileSettingsTree::COL_QUANTITY);

        if (colSku == -1)
            continue;

        for (const auto &line : rode->lines) {
            if (line.size() <= colSku)
                continue;
            const QString sku = line[colSku].trimmed();
            if (sku.isEmpty())
                continue;

            Record rec;
            rec.sku           = sku;
            rec.date          = fileDate;
            rec.titlePriority = titlePriority;
            rec.fileName      = fileName;

            if (colTitle != -1 && line.size() > colTitle)
                rec.title = line[colTitle].trimmed();

            if (colQty != -1 && line.size() > colQty) {
                bool ok;
                double q = QString(line[colQty])
                        .replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
                if (ok && q > 0)
                    rec.quantity = static_cast<int>(q);
            }

            if (colWeight != -1 && line.size() > colWeight) {
                bool ok;
                double w = QString(line[colWeight])
                        .replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
                if (ok && w > 0.0)
                    rec.weightKg = w / 1000.0;  // grams → kg
            }

            if (colCurrency != -1 && line.size() > colCurrency)
                rec.invoiceCurrency = line[colCurrency].trimmed();

            if (colPrice != -1 && line.size() > colPrice) {
                bool ok;
                double price = QString(line[colPrice])
                        .replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
                if (ok && price > 0.0) {
                    rec.origUnitPrice = price;
                    rec.unitPrice     = price;  // default: same as invoice price

                    // Convert when all four conditions hold.
                    const bool needsConversion = rateManager
                            && !companyCurrency.isEmpty()
                            && !rec.invoiceCurrency.isEmpty()
                            && rec.invoiceCurrency != companyCurrency;
                    if (needsConversion) {
                        rec.unitPrice    = rateManager->convert(
                                price, rec.invoiceCurrency, companyCurrency, fileDate);
                        rec.origCurrency = rec.invoiceCurrency;
                    }
                }
            }

            result.append(rec);
        }
    }

    return result;
}
