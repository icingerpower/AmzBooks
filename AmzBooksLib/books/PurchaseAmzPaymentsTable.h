#ifndef PURCHASEAMZPAYMENTSTABLE_H
#define PURCHASEAMZPAYMENTSTABLE_H

#include "AbstractBooksTable.h"
#include "AmzPaymentSettings.h"
#include "PurchaseAmzPaymentsManager.h"

#include <QHash>

class CurrencyRateManager;

class PurchaseAmzPaymentsTable : public AbstractBooksTable
{
    Q_OBJECT

public:
    explicit PurchaseAmzPaymentsTable(
            const BooksConnections *bookConnections,
            const QDir &workingDir,
            QObject *parent = nullptr);

    QString getId() const override;
    void load(int year) override;

    void add(const QString &sourceFilePath, const AmzPaymentInfo &info);
    void removePayment(const QModelIndex &index);
    void removePayment(const QString &rowId);

    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

private:
    PurchaseAmzPaymentsManager &manager() const;
    PurchaseAmzPaymentsManager *m_manager;
    AmzPaymentSettings         *m_settings;
    QDir m_workingDir;
    QString m_companyCurrency;
    CurrencyRateManager *m_currencyRateManager;
    QHash<QString, double> m_convertedAmounts;
};

#endif // PURCHASEAMZPAYMENTSTABLE_H
