#ifndef PURCHASECSVLOADER_H
#define PURCHASECSVLOADER_H

#include <QDate>
#include <QDir>
#include <QList>
#include <QString>

class CurrencyRateManager;

/*
 * PurchaseCsvLoader — shared utility for loading purchase records from CSV files.
 *
 * Both InventoryMoveTree and InventoryTable parse purchase CSV files with
 * identical per-row logic (SKU, title, date, price, currency, weight, quantity).
 * parseFiles() centralises that parsing; each consumer applies its own policy:
 *
 *   InventoryMoveTree — "latest price": for each SKU the first (newest) valid
 *                        price encountered wins.  Title selection is independent:
 *                        highest-language-priority file wins (FR > US/CA/COM >
 *                        other), regardless of date.
 *
 *   InventoryTable    — FIFO: all batches are kept, ordered newest-first, so
 *                       the build step can match current stock against purchases
 *                       starting from the most recent purchase.
 *
 * ── Currency conversion ──────────────────────────────────────────────────────
 * When companyCurrency and rateManager are both provided (non-empty / non-null),
 * and a record's invoice currency differs from companyCurrency, unitPrice is
 * converted using the rate for the record's date.  The raw invoice price is
 * preserved in origUnitPrice and the invoice currency in origCurrency.
 * When no conversion applies, unitPrice == origUnitPrice and origCurrency is
 * empty.  invoiceCurrency is always set from the CSV column when present.
 *
 * ── File ordering ────────────────────────────────────────────────────────────
 * Records are returned in the order files are provided; within a file, rows
 * follow CSV order.  The caller is responsible for passing files in the desired
 * order (typically newest-first, achieved by a lexicographic-descending sort on
 * the bare filename that starts with the YYYY-MM-DD__ date prefix).
 */
class PurchaseCsvLoader
{
public:
    struct Record {
        QString sku;
        QString title;
        // Derived from the source filename suffix (case-insensitive):
        // FR=3 (highest), US/CA/COM=2, anything else=1.
        // Used by InventoryMoveTree for language-priority title selection.
        int     titlePriority = 1;
        QDate   date;               // from YYYY-MM-DD__ filename prefix
        double  unitPrice = 0.0;    // in companyCurrency (equals origUnitPrice when no conversion)
        double  origUnitPrice = 0.0;// raw invoice unit price before conversion
        QString invoiceCurrency;    // ISO-4217 code from CSV; always set when column exists
        QString origCurrency;       // invoice currency; set only when conversion was applied
        double  weightKg = 0.0;     // unit weight in kg (CSV column values are in grams → ÷ 1000)
        int     quantity = 0;       // purchase quantity; 0 when the column is absent
        QString fileName;           // bare filename of the source CSV
    };

    // Parses every CSV row from filePaths (in the order provided) using
    // PurchaseFileSettingsTree(settingsDir) for column-name resolution.
    //
    // When companyCurrency is non-empty and rateManager is non-null, each
    // record's unitPrice is converted from its invoice currency to
    // companyCurrency at the rate for the record's date.  origUnitPrice and
    // origCurrency are set only when conversion was actually applied.
    //
    // Files without a YYYY-MM-DD__ date prefix throw ExceptionWithTitleText.
    // Files that cannot be read are silently skipped.
    // Files without a recognised SKU column are silently skipped.
    static QList<Record> parseFiles(const QStringList &filePaths,
                                    const QDir &settingsDir,
                                    const QString &companyCurrency = QString(),
                                    const CurrencyRateManager *rateManager = nullptr);
};

#endif // PURCHASECSVLOADER_H
