#ifndef PURCHASECONTROLTABLE_H
#define PURCHASECONTROLTABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QList>
#include <QString>
#include <QStringList>

// Stores per-supplier purchase control entries: which month (or every month)
// a supplier should be checked. Persisted in purchase_control.csv.
//
// CSV column IDs are stable strings (never translated) so the file survives
// column reordering or future additions. Qt::EditRole returns the stable code;
// Qt::DisplayRole returns the translated display text.
class PurchaseControlTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    // Stable frequency codes stored in the CSV and returned by Qt::EditRole.
    // Never change these values — they are part of the file format.
    static constexpr const char *FREQ_ALL = "all";
    static constexpr const char *FREQ_JAN = "jan";
    static constexpr const char *FREQ_FEB = "feb";
    static constexpr const char *FREQ_MAR = "mar";
    static constexpr const char *FREQ_APR = "apr";
    static constexpr const char *FREQ_MAY = "may";
    static constexpr const char *FREQ_JUN = "jun";
    static constexpr const char *FREQ_JUL = "jul";
    static constexpr const char *FREQ_AUG = "aug";
    static constexpr const char *FREQ_SEP = "sep";
    static constexpr const char *FREQ_OCT = "oct";
    static constexpr const char *FREQ_NOV = "nov";
    static constexpr const char *FREQ_DEC = "dec";

    // Returns all frequency codes in display order (for populating combo boxes).
    static QStringList allFrequencyCodes();

    // Returns the translated display text for a frequency code.
    // Safe to call with an unknown code — returns the code itself as fallback.
    static QString frequencyDisplayText(const QString &code);

    explicit PurchaseControlTable(const QDir &workingDir, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // Appends a new entry and saves.
    void addEntry(const QString &supplierAccount, const QString &label, const QString &frequencyCode);

    // Removes row and saves. Returns false if row is out of range.
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

private:
    struct ControlEntry {
        QString supplierAccount;
        // Optional label filter: when non-empty, a match requires a purchase invoice
        // for this supplier whose label contains this string (case-insensitive).
        QString label;
        QString frequencyCode; // stable code, e.g. "all", "jan" …
    };

    QDir m_workingDir;
    QList<ControlEntry> m_data;

    // Stable column IDs written to the CSV header — never translated.
    static const QString COL_ID_SUPPLIER;
    static const QString COL_ID_LABEL;
    static const QString COL_ID_FREQUENCY;

    void _load();
    void _save() const;
    QString _csvFilePath() const;
};

#endif // PURCHASECONTROLTABLE_H
