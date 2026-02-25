#ifndef PURCHASEAMZPAYMENTSMANAGER_H
#define PURCHASEAMZPAYMENTSMANAGER_H

#include <QAbstractTableModel>
#include <QDir>
#include <QString>
#include <QDate>
#include <QList>

struct AmzPaymentInfo {
    QString countryCode;           // Marketplace code e.g. "com", "co_uk", "de"
    QDate dateFrom;
    QDate dateTo;
    double balanceStart = 0.0;
    QString balanceStartCurrency;
    bool hasBalanceStart = false;  // false → absent from filename (treated as 0)
    double balanceEnd = 0.0;
    QString balanceEndCurrency;
    bool hasBalanceEnd = false;    // false → absent from filename (treated as 0)
    // Note: balance-begin and balance-end must both be present or both absent
    double expenses = 0.0;
    QString expensesCurrency;
    bool hasExpenses = false;
    double refundedExpenses = 0.0;
    QString refundedExpensesCurrency;
    bool hasRefundedExpenses = false;
    double paid = 0.0;
    QString paidCurrency;
    QString filePath;
};

// Filename format:
// payment_{marketplace}_{YYYY}_{MM}_{DD}__to__{YYYY}_{MM}_{DD}
//   [__balance-begin-{amount}{CUR}]      (optional; must be paired with balance-end)
//   [__balance-end-{amount}{CUR}]        (optional; must be paired with balance-begin)
//   [__expenses-{amount}{CUR}]           (optional if proxy < 200 EUR equivalent)
//   [__refunded-expenses-{amount}{CUR}]  (always optional)
//   __{paid_amount}{CUR}
// balance-begin and balance-end must both be present or both absent; UI warns when both absent

class PurchaseAmzPaymentsManager : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit PurchaseAmzPaymentsManager(const QDir &workingDir, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    QList<AmzPaymentInfo> getPayments(const QDate &from, const QDate &to) const;
    const QList<AmzPaymentInfo> &allPayments() const;

    void add(const QString &sourceFilePath, const AmzPaymentInfo &info);
    bool remove(const QString &fileName);

    static AmzPaymentInfo decode(const QString &filePath);
    static QString encode(const AmzPaymentInfo &info);
    static QString getRelativePath(const AmzPaymentInfo &info);

    // Fixed approximate conversion rate to EUR (good enough for threshold checks)
    static double toEur(double amount, const QString &currency);

private:
    QDir m_workingDir;
    QList<AmzPaymentInfo> m_data;
    static const QStringList HEADER;

    void _load();
    void scanDirectory(const QDir &dir);
};

#endif // PURCHASEAMZPAYMENTSMANAGER_H
