#ifndef PURCHASEINVOICEMANAGER_H
#define PURCHASEINVOICEMANAGER_H

#include <QAbstractTableModel>
#include <QDir>
#include <QString>
#include <QStringList>
#include <QDate>
#include <QList>

struct PurchaseInformation {
    QDate date;
    QString account;
    QString label;
    QString supplier;
    QStringList vatTokens;
    QHash<QString, QHash<QString, double>> country_vatRate_vat;
    double totalAmount = 0.0;
    QString rawTotalAmount; // To preserve formatting (e.g. "10.0")
    QString currency;
    QString originalExtension; // e.g. "pdf"
    QString filePath; // Absolute path to the file
    bool isInventory = false; // If stock in filename
    bool isDDP = false; // if DDP in file name
    QString countryCodeFrom; // If ends with 2 country code we fill countryCodeFrom and countryCodeTo
    QString countryCodeTo;
};

class PurchaseInvoiceManager : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit PurchaseInvoiceManager(const QDir &workingDir, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void add(const QString &sourceFilePath, PurchaseInformation &info);

    QList<PurchaseInformation> getInvoices(const QDate &from, const QDate &to) const;

    static QString encode(const PurchaseInformation &info);
    static PurchaseInformation decode(const QString &fileName);
    // Helpers for storage path
    static QString getRelativePath(const PurchaseInformation &info);


private:
    QDir m_workingDir;
    QList<PurchaseInformation> m_data;
    static const QStringList HEADER;

    void _load();
    void scanDirectory(const QDir &dir);
};

#endif // PURCHASEINVOICEMANAGER_H
