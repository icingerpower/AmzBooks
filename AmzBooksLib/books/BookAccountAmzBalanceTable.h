#ifndef BOOKACCOUNTAMZBALANCETABLE_H
#define BOOKACCOUNTAMZBALANCETABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QVariant>
#include <QCoroTask>
#include <functional>

class BookAccountAmzBalanceTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    struct Accounts {
        QString balanceAccount;
        QString account;
    };

    explicit BookAccountAmzBalanceTable(const QDir &workingDir, QObject *parent = nullptr);

    QCoro::Task<Accounts> getAccount(const QString &amazonSite, std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing = nullptr) const;
    Accounts getAccountSync(const QString &amazonSite) const; // Returns empty Accounts if not found

    void addAmazon(const QString &amazonSite);
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    // Header:
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Basic functionality:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Editable:
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    static const QStringList HEADER_IDS;
    
    QList<QStringList> m_listOfStringList;
    QString m_filePath;
    
    void _fillIfEmpty();
    void _save();
    void _load();
    
    // Cache helper
    int _findRow(const QString &amazonSite) const;
};

#endif // BOOKACCOUNTAMZBALANCETABLE_H
