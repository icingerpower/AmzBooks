#ifndef SALEBOOKACCOUNTSTABLE_H
#define SALEBOOKACCOUNTSTABLE_H

#include <QAbstractTableModel>
#include <QDir>

#include "VatCountries.h"
#include "model/TaxScheme.h"
#include "CountriesEu.h"
#include <QException>

#include "ExceptionTaxSchemeInvalid.h"
#include <QCoroTask>
#include <functional>


//
// Purpose:
// Central registry that maps a sale “VAT situation” to bookkeeping accounts.
// Given a TaxScheme + countries (and optionally customer account / VAT rate), it returns:
//  - the sales account (class 6/7 depending on your chart of accounts)
//  - the VAT account (class 4), if the sale generates VAT to be booked.
//
// Key idea:
// resolveVatCountries() normalizes raw (from/to) inputs into a *stable and minimal* key,
// collapsing dimensions that are irrelevant for a given TaxScheme (e.g. exports, deemed-supplier),
// while keeping destination-country granularity only when legally required (OSS/IOSS).
//
// Result:
// Avoids account explosion (one account per route) while staying consistent for VAT reporting.


class SaleBookAccountsTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    struct Accounts{
        QString saleAccount;
        QString vatAccount;
        QString vatAccountToPay;
    };
    explicit SaleBookAccountsTable(const QDir &workingDir, QObject *parent = nullptr);
    VatCountries resolveVatCountries(TaxScheme taxScheme, const QString &countryFrom, const QString &countryCodeTo) const;

    QCoro::Task<SaleBookAccountsTable::Accounts> getAccounts(const VatCountries &vatCountries, double vatRate, std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing = nullptr) const;
    void addAccount(const VatCountries &vatCountries, double vatRate, const SaleBookAccountsTable::Accounts &accounts);
    
    // Header:
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Basic functionality:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Editable:
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    static const QStringList HEADER;
    QList<QStringList> m_listOfStringList;
    QHash<VatCountries, QHash<QString, Accounts>> m_vatCountries_vatRate_accountsCache;

    void _fillIfEmpty();
    void _save();
    void _load();
    void _rebuildCache();
    QString m_filePath;
};

#endif // SALEBOOKACCOUNTSTABLE_H
