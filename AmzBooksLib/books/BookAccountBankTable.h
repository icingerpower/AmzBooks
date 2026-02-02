#ifndef BOOKACCOUNTBANKTABLE_H
#define BOOKACCOUNTBANKTABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QList>
#include <QString>

struct BankAccountItem {
    QString bankName;
    QString account;
    QString feesAccount;
    QString id; // Hidden, used for loading/saving stability
};

class BookAccountBankTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit BookAccountBankTable(const QDir &workingDir, QObject *parent = nullptr);

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    QDir m_workingDir;
    QString m_filePath;
    QList<BankAccountItem> m_data;

    void _load();
    void _save();
    void _init(); // Populates with ALL_BANKS, default values
};

#endif // BOOKACCOUNTBANKTABLE_H
