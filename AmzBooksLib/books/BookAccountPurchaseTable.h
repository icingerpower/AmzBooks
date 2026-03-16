#ifndef BOOKACCOUNTPURCHASETABLE_H
#define BOOKACCOUNTPURCHASETABLE_H

#include <QAbstractTableModel>
#include <QDir>
#include <QHash>
#include <QSet>

class BookAccountPurchaseTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit BookAccountPurchaseTable(const QDir &workingDir
                                       , const QString &countryCodeCompany, QObject *parent = nullptr);

    // Returned by the Closest variants: matched account plus the stored VAT rate actually
    // found (may differ slightly from the queried rate when currencies differ).
    struct ClosestResult {
        QString account;
        double  matchedRate = 0.0; // stored rate, e.g. 0.20 for 20 %
    };

    // Find the account whose stored rate is closest to vatRate (in decimal).
    // maxDiffRateAllowed is in percentage points (default 0.3 %).
    // Throws ExceptionWithTitleText when no entry is within tolerance.
    // Recommended to use 0.49 when the VAT amount is very small (< ~3 EUR).
    ClosestResult getAccountsDebit6Closest(const QString &countryCode, double vatRate, double maxDiffRateAllowed = 0.3) const;
    QString       getAccountsDebit6(const QString &countryCode, double vatRate) const;
    ClosestResult getAccountsCredit4Closest(const QString &countryCode, double vatRate, double maxDiffRateAllowed = 0.3) const;
    QString       getAccountsCredit4(const QString &countryCode, double vatRate) const;

    void addAccount(const QString &countryCode
                    , double vatRate
                    , const QString &vatAccountDebit6
                    , const QString &vatAccountCredit4);

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

    struct AccountPair {
        QString debit6;
        QString credit4;
    };
    QHash<QString, AccountPair> m_cache;
    QSet<QString> m_existenceCache; // Key: "Country|Rate"

    // Shared search: find the cache key for the closest rate entry within tolerance.
    // Returns an empty string and sets minDiff if nothing is within maxDiffRateAllowed.
    QString _findClosestKey(const QString &countryCode, double vatRate,
                            double maxDiffRateAllowed, double &outMinDiff) const;

    void _fillIfEmpty();
    void _save();
    void _load();
    void _rebuildCache();
    QString m_filePath;
    QString m_countryCodeCompany;
};

#endif // BOOKACCOUNTPURCHASETABLE_H
