#ifndef BOOKSACCOUNTSSALESTABLE_H
#define BOOKSACCOUNTSSALESTABLE_H

#include <QAbstractTableModel>
#include <QDir>

#include "VatCountries.h"
#include "books/TaxScheme.h"
#include "CountriesEu.h"
#include "orders/SaleType.h"
#include <QException>
#include "ExceptionWithTitleText.h"
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


class BooksAccountsSalesTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    struct Accounts{
        QString saleAccount;
        QString vatAccount;
        QString vatAccountToPay;
        // Broad client grouping account (e.g. "CCLIENTEU" for EU buyers, "CLIENTDOM" for domestic).
        // Used as account2 in order/service journal lines.
        QString clientAccount;
    };
    explicit BooksAccountsSalesTable(const QDir &workingDir, QObject *parent = nullptr);
    VatCountries resolveVatCountries(TaxScheme taxScheme, const QString &companyCountryFrom, const QString &countryFrom, const QString &countryCodeTo) const;

    // saleType defaults to Products for backward compatibility with callers that don't specify it.
    QCoro::Task<BooksAccountsSalesTable::Accounts> getAccounts(const VatCountries &vatCountries, double vatRate, SaleType saleType = SaleType::Products, std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing = nullptr) const;
    // Synchronous best-effort lookup; returns empty Accounts if not found (no user prompt).
    Accounts getAccountsIfPresent(const VatCountries &vatCountries, double vatRate, SaleType saleType = SaleType::Products) const;
    void addAccount(const VatCountries &vatCountries, double vatRate, const BooksAccountsSalesTable::Accounts &accounts, SaleType saleType = SaleType::Products);
    
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
    QHash<VatCountries, QHash<SaleType, QHash<QString, Accounts>>> m_vatCountries_vatRate_accountsCache;

    void _fillIfEmpty();
    void _save();
    void _load();
    void _rebuildCache();
    void _sort();
    QString m_filePath;
};

#endif // BOOKSACCOUNTSSALESTABLE_H
