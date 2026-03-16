#ifndef PURCHASEINVOICEMANAGER_H
#define PURCHASEINVOICEMANAGER_H

#include <QAbstractTableModel>
#include <QDir>
#include <QString>
#include <QStringList>
#include <QDate>
#include <QList>

class BookAccountPurchaseTable;

struct PurchaseInformation {
    QDate date;
    QString account;
    QString label;
    QString accountSupplier;
    QStringList vatTokens;
    QHash<QString, QHash<QString, double>> country_vatRate_vat;
    // For dual-amount VAT tokens (e.g. "FR-TVA-2.21EUR_9.28PLN"): VAT expressed in
    // company currency, keyed identically to country_vatRate_vat. Empty for classic
    // single-amount tokens. When non-empty, JournalEntryFactory uses the ratio
    // vatCompany/vatSource as the exact exchange rate instead of CurrencyRateManager.
    QHash<QString, QHash<QString, double>> country_vatRate_vatCompany;
    double totalAmount = 0.0;
    QString rawTotalAmount; // To preserve formatting (e.g. "10.0")
    QString currency;
    QString rawVatAmount; // e.g. "13.6" – total VAT amount (simple representation)
    QString vatCurrency;  // e.g. "EUR" – currency of the VAT amount
    QString vatCountry;   // e.g. "FR"  – country code for the VAT (EU + GB)
    QString originalExtension; // e.g. "pdf"
    QString filePath; // Absolute path to the file
    bool isInventory = false; // If stock in filename
    bool isDDP = false; // if DDP in file name
    QString countryCodeFrom; // If ends with 2 country code we fill countryCodeFrom and countryCodeTo
    QString countryCodeTo;
    QHash<QString, double> subUntaxedAmount; // Extra untaxed amounts per account
};

class PurchaseInvoiceManager : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit PurchaseInvoiceManager(const QDir &workingDir, QObject *parent = nullptr);

    bool isSupplierWithCountries(const QString &supplierAccount) const;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void add(const QString &sourceFilePath, PurchaseInformation &info);
    bool remove(const QString &fileName);

    QList<PurchaseInformation> getInvoices(const QDate &from, const QDate &to) const;

    static QString encode(const PurchaseInformation &info);
    static PurchaseInformation decode(const QString &fileName, const BookAccountPurchaseTable *purchaseTable = nullptr);
    // Helpers for storage path
    static QString getRelativePath(const PurchaseInformation &info);


private:
    QDir m_workingDir;
    QList<PurchaseInformation> m_data;
    QSet<QString> m_suppliersWithCountries;
    static const QStringList HEADER;

    void _load();
    void scanDirectory(const QDir &dir);
};

#endif // PURCHASEINVOICEMANAGER_H
