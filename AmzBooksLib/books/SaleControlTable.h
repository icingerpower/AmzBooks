#ifndef SALECONTROLTABLE_H
#define SALECONTROLTABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QList>
#include <QString>
#include <QStringList>

// Stores per-store sale control entries: which kind of activity (sale, refund,
// or both) is expected for a given store when generating bookkeeping.
// Persisted in control_sales.csv.
//
// CSV column IDs are stable strings (never translated) so the file survives
// column reordering or future additions. Qt::EditRole returns the stable code;
// Qt::DisplayRole returns the translated display text.
class SaleControlTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    // Stable sale-type codes stored in the CSV and returned by Qt::EditRole.
    // Never change these values — they are part of the file format.
    static constexpr const char *SALE_TYPE_BOTH   = "both";
    static constexpr const char *SALE_TYPE_SALE   = "sale";
    static constexpr const char *SALE_TYPE_REFUND = "refund";

    // Returns all sale-type codes in display order (for populating combo boxes).
    static QStringList allSaleTypeCodes();

    // Returns the translated display text for a sale-type code.
    // Safe to call with an unknown code — returns the code itself as fallback.
    static QString saleTypeDisplayText(const QString &code);

    explicit SaleControlTable(const QDir &workingDir, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // Appends a new entry and saves.
    void addEntry(const QString &storeName, const QString &saleTypeCode);

    // Removes row and saves. Returns false if row is out of range.
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

private:
    struct ControlEntry {
        QString storeName;
        // Stable code: "both", "sale", or "refund".
        QString saleTypeCode;
    };

    QDir m_workingDir;
    QList<ControlEntry> m_data;

    // Stable column IDs written to the CSV header — never translated.
    static const QString COL_ID_STORE;
    static const QString COL_ID_SALE_TYPE;

    void _load();
    void _save() const;
    QString _csvFilePath() const;
};

#endif // SALECONTROLTABLE_H
