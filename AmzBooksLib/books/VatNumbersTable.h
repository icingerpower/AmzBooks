#ifndef VATNUMBERSTABLE_H
#define VATNUMBERSTABLE_H

#include <QAbstractTableModel>
#include <QString>
#include <QList>

class VatNumbersTable : public QAbstractTableModel
{
    Q_OBJECT

public:

    static const QStringList HEADER_IDS;
    explicit VatNumbersTable(const QString &filePath, QObject *parent = nullptr);

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

    QString m_filePath;
    QList<VatItem> m_data; 
    
    QString m_emptyString;
};

#endif // VATNUMBERSTABLE_H
