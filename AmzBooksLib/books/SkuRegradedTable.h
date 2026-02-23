#ifndef SKUREGRADEDTABLE_H
#define SKUREGRADEDTABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QList>
#include <QString>
#include <QStringList>

/*
 * SkuRegradedTable — persistent mapping from Amazon-regraded SKUs to their
 * original canonical SKUs.
 *
 * Amazon marks regraded inventory with an "amzn.gr." prefix.  This table
 * lets the user record the association between the regraded name (as it
 * appears in Amazon reports) and the original SKU used in purchase invoices.
 *
 * ── CSV format ──────────────────────────────────────────────────────────────
 * File: <workingDir>/regraded_skus.csv
 * Separator: semicolon (;)
 * Header: "SKU regraded;SKU"
 * Example:
 *   amzn.gr.A5-BOOK-COVER-DESIGN-5-QaQJXV-PO;A5-BOOK-COVER-DESIGN-5
 *
 * ── Editability ─────────────────────────────────────────────────────────────
 * Column 0 (SKU regraded): read-only in the view. New rows are appended via
 *   appendRegradedSku(); existing keys cannot be changed through the model.
 * Column 1 (SKU):          editable. The user fills in the canonical SKU that
 *   matches the regraded entry.
 */
class SkuRegradedTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Columns {
        COL_SKU_REGRADED = 0,
        COL_SKU,
        COL_COUNT
    };

    explicit SkuRegradedTable(const QDir &workingDir, QObject *parent = nullptr);

    // Append a new row [regradedSku, ""] if regradedSku is not already present.
    // Does nothing and returns false if the key already exists.
    bool appendRegradedSku(const QString &regradedSku);

    // Returns true if regradedSku is present in the table.
    bool contains(const QString &regradedSku) const;

    // Returns the canonical SKU mapped to regradedSku, or an empty string if
    // regradedSku is not present or its mapping has not been filled in yet.
    QString getSku(const QString &regradedSku) const;

    // QAbstractTableModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    // Each entry: [regradedSku, canonicalSku]
    QList<QStringList> m_rows;
    QString m_filePath;

    // Returns the row index for regradedSku, or -1 if not found.
    int _findRow(const QString &regradedSku) const;
    void _save();
    void _load();
};

#endif // SKUREGRADEDTABLE_H
