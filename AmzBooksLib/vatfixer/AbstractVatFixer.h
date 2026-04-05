#ifndef ABSTRACTVATFIXER_H
#define ABSTRACTVATFIXER_H

#include <QDate>
#include <QDir>
#include <QList>
#include <QString>
#include <QStringList>

// Per-order discrepancy between Amazon's reported VAT (from the taxually
// source TXT) and the VAT that taxually declares (from the ReturnAnalytics
// xlsx summary).  One entry covers all TXT lines for the same order ID.
struct VatOrderEntry {
    QString orderId;
    QString sourceFile;       // bare filename of the matching TXT file
    QString transactionType;  // e.g. "Distance sale (OSS)"
    QDate   date;
    QString taxCode;          // e.g. "EU|SA|20.00|G|DE|FR"
    double  taxRate = 0.0;    // decimal fraction, e.g. 0.20

    // Display fields sourced from the TXT lines for this order
    QString marketplace;
    QString sku;              // from first matching TXT row
    QString description;      // from first matching TXT row
    QString departureCountry;
    QString arrivalCountry;

    // VAT amounts (aggregated across all TXT lines for the order)
    double netAmount   = 0.0; // sum of box values from ReturnAnalytics
    double taxuallyVat = 0.0; // netAmount × taxRate (taxually's declaration)
    double amazonVat   = 0.0; // TOTAL_ACTIVITY_VALUE_VAT_AMT from TXT

    double difference() const { return taxuallyVat - amazonVat; }

    // Action chosen by the user in DialogValidOrders
    enum class Action { Fix, Remove };
    Action action = Action::Fix;
};

class AbstractVatFixer
{
public:
    AbstractVatFixer() = default;
    virtual ~AbstractVatFixer() = default;

    // Human-readable name shown in the UI fixer selector (e.g. "Taxually").
    virtual QString getName() const = 0;

    // Returns true if this fixer knows how to handle the given inventory file.
    virtual bool isInventoryFile(const QString &filePath) const = 0;
    virtual QStringList getInventoryValidExtensions() const = 0;

    // Returns true if this fixer knows how to handle the given VAT-order file.
    virtual bool isOrderFile(const QString &filePath) const = 0;
    virtual QStringList getVatOrdersValidExtensions() const = 0;

    // File extensions accepted for the VAT summary (ReturnAnalytics) file.
    virtual QStringList getVatSummaryValidExtensions() const = 0;

        // Result of fixInventoryValue: one FixedSku per row that received a price,
    // plus the list of SKUs for which no price could be found.
    struct InventoryFixResult {
        struct FixedSku {
            QString sku;
            double  price     = 0.0;
            QString currency;
            // Bare filename of the invoice (xlsx or purchase CSV) that provided
            // the price.  Empty when no price was found.
            QString sourceFile;
        };
        QList<FixedSku> fixedSkus;
        QStringList     skusNotFound;
    };

    // Read the inventory xlsx at filePath, fill the empty purchase-price cells
    // in the "Missing Sku list" sheet.  Prices are looked up first in the xlsx
    // "Inventory" sheet, then in purchaseCsvFiles (parsed with settingsDir for
    // column-name resolution).  Writes the result to newFilePath.
    virtual InventoryFixResult fixInventoryValue(
        const QString     &filePath,
        const QString     &newFilePath,
        const QStringList &purchaseCsvFiles,
        const QDir        &settingsDir) const = 0;

    // Read every ReturnAnalytics xlsx in vatFilePathsSummary, aggregate all
    // per-order VAT discrepancies against each TXT in vatFilePathToUpdate,
    // show a single DialogValidOrders for all of them, then write fixed copies
    // of the TXT files (original basename + postFixBaseNameUpdated).
    virtual void fixVatOrders(const QStringList &vatFilePathsSummary,
                               const QStringList &vatFilePathToUpdate,
                               const QString &postFixBaseNameUpdated) const = 0;

    // --- Recorder / self-registration ---
    class Recorder {
    public:
        explicit Recorder(AbstractVatFixer *fixer);
    };

    static const QList<const AbstractVatFixer *> &ALL_FIXERS();

private:
    static QList<const AbstractVatFixer *> &getFixers();
};

// Place in the .cpp of each concrete fixer to auto-register at startup.
#define DECLARE_VAT_FIXER(NEW_CLASS) \
    static NEW_CLASS instance##NEW_CLASS; \
    static AbstractVatFixer::Recorder recorder##NEW_CLASS{&instance##NEW_CLASS};

#endif // ABSTRACTVATFIXER_H
