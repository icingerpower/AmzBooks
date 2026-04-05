#ifndef VATFIXERTAXUALLY_H
#define VATFIXERTAXUALLY_H

#include "vatfixer/AbstractVatFixer.h"

// Concrete VAT fixer for the taxually workflow:
//   - Inventory file: taxually inventory xlsx with "Inventory" and
//     "Missing Sku list" sheets.
//   - VAT-order file: taxually automated-download TSV reports (.txt).
//   - Summary file: ReturnAnalytics xlsx with "Tax return detail" sheet.
class VatFixerTaxually : public AbstractVatFixer
{
public:
    QString getName() const override;

    // --- File-type detection ---
    bool isInventoryFile(const QString &filePath) const override;
    QStringList getInventoryValidExtensions() const override;

    bool isOrderFile(const QString &filePath) const override;
    QStringList getVatOrdersValidExtensions() const override;
    QStringList getVatSummaryValidExtensions() const override;

    // --- Operations ---
    // Opens the taxually inventory xlsx, reads purchase prices from the
    // "Inventory" sheet and from purchaseCsvFiles, fills empty Purchase price
    // (col F) cells in the "Missing Sku list" sheet, and saves the result to
    // newFilePath.  Returns a result with fixed SKUs (each with its source file)
    // and SKUs for which no price could be found.
    InventoryFixResult fixInventoryValue(
        const QString     &filePath,
        const QString     &newFilePath,
        const QStringList &purchaseCsvFiles,
        const QDir        &settingsDir) const override;

    // Reads vatFilePathSummary (ReturnAnalytics xlsx), compares per-order VAT
    // against TXT files in vatFilePathToUpdate, shows DialogValidOrders for
    // differences > 0.10 EUR, then writes fixed TXT files named
    // <original_basename><postFixBaseNameUpdated>.
    void fixVatOrders(const QStringList &vatFilePathsSummary,
                      const QStringList &vatFilePathToUpdate,
                      const QString &postFixBaseNameUpdated) const override;

    // Returns all VAT discrepancies (|diff| > 0.10 EUR) without showing any
    // dialog.  Intended for unit tests and as the core of fixVatOrders.
    QList<VatOrderEntry> findVatDiscrepancies(
        const QString     &vatFilePathSummary,
        const QStringList &vatFilePathToUpdate) const;

    // Write a fixed version of txtPath: skip rows whose orderId is in
    // removeIds, or update VAT fields for orderId entries in fixRates.
    // Public so the write step can be tested independently of the dialog.
    void writefixedTxt(const QString &txtPath,
                       const QString &fixedPath,
                       const QSet<QString> &removeIds,
                       const QHash<QString, double> &fixRates) const;

private:
    // Helpers for fixVatOrders ------------------------------------------------

    // Parse the "Tax return detail" sheet of a ReturnAnalytics xlsx.
    // Returns one VatOrderEntry per (orderId, sourceFile) pair,
    // aggregating box-value sums and keeping the tax code of the first row.
    QList<VatOrderEntry> _parseReturnAnalytics(const QString &xlsxPath) const;

    // Parse a taxually TXT file (tab-separated).
    // Returns a map orderId → {amazonVat, marketplace, sku, description,
    //                          departureCountry, arrivalCountry}.
    struct TxtOrderInfo {
        double  amazonVat = 0.0;
        QString marketplace;
        QString sku;
        QString description;
        QString departureCountry;
        QString arrivalCountry;
    };
    QHash<QString, TxtOrderInfo> _parseTxtFile(const QString &txtPath) const;
};

#endif // VATFIXERTAXUALLY_H
