#ifndef VATNUMBERSTABLE_H
#define VATNUMBERSTABLE_H

#include <QAbstractTableModel>
#include <QString>
#include <QList>

class VatNumbersTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit VatNumbersTable(const QString &iniFilePath, QObject *parent = nullptr);

    const QString &getVatNumber(const QString &countryCode) const;
    bool hasVatNumber(const QString &countryCode) const;
    void addVatNumber(const QString &country, const QString &vatNumber);

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

private:
    struct VatItem {
        QString country; // Col 0
        QString vatNumber; // Col 1
        QString id; // Col 2 (Hidden)
    };

    void _load();
    void _save();

    QString m_iniFilePath;
    QList<VatItem> m_data; // Sorted? Not strictly required by prompt, but usually good. Prompt "No method to add data is needed" in previous task, but here "We can add one vat number... method const QString & getVatNumber".
    
    // Cache for getVatNumber to return const reference
    // Actually, can return reference to m_data item if stable? 
    // QList reallocates. 
    // "make sure data are saved to we can return a const QString &" - probably means m_data should persist.
    // If I return reference to m_data member, it's unsafe if m_data changes.
    // Use a static empty string for not found? Or member empty string.
    QString m_emptyString;
};

#endif // VATNUMBERSTABLE_H
