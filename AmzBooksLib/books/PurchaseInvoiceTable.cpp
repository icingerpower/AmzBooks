#include "PurchaseInvoiceTable.h"
#include <QFileInfo>

PurchaseInvoiceTable::PurchaseInvoiceTable(
        const BooksConnections *bookConnections,
        const QDir &workingDir,
        QObject *parent)
    : AbstractBooksTable(bookConnections, workingDir, parent)
    , m_workingDir(workingDir)
{
    m_manager = new PurchaseInvoiceManager(workingDir, this);
}

QString PurchaseInvoiceTable::getId() const
{
    return "purchase-invoices";
}

void PurchaseInvoiceTable::load(int year)
{
    QDate start(year, 1, 1);
    QDate end(year, 12, 31);
    
    QList<PurchaseInformation> invoices = m_manager->getInvoices(start, end);
    
    for (const auto &info : invoices) {
        // We use the file name as the Row ID
        QFileInfo fi(info.filePath);
        QString rowId = fi.fileName();
        
        double vatOrig = 0.0;
        QString vatCountry;
        QString vatCurrency = info.currency; // Assume same currency for VAT as Invoice
        
        // Just take the first non-zero VAT found for display purposes, or sum them.
        if (!info.country_vatRate_vat.isEmpty()) {
            vatCountry = info.country_vatRate_vat.keys().first();
            for (auto it = info.country_vatRate_vat[vatCountry].begin(); it != info.country_vatRate_vat[vatCountry].end(); ++it) {
                vatOrig += it.value();
            }
        }
        
        add(rowId, "",
            info.date,
            info.totalAmount,
            info.currency,
            info.label,
            info.account,
            info.accountSupplier,
            vatOrig,
            vatCountry,
            vatCurrency);
    }
}

PurchaseInvoiceManager &PurchaseInvoiceTable::manager() const
{
    return *m_manager;
}

void PurchaseInvoiceTable::removeInvoice(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }
    
    QString rowId = getRowId(index);
    if (m_manager->remove(rowId)) {
        remove(rowId);
    }
}

int PurchaseInvoiceTable::columnCount(const QModelIndex &parent) const
{
    return AbstractBooksTable::columnCount(parent) + 3;
}

QVariant PurchaseInvoiceTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        int baseCols = AbstractBooksTable::columnCount();
        if (section == baseCols) return tr("Country From");
        if (section == baseCols + 1) return tr("Country To");
        if (section == baseCols + 2) return tr("VAT Rate");
    }
    return AbstractBooksTable::headerData(section, orientation, role);
}

QVariant PurchaseInvoiceTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    int baseCols = AbstractBooksTable::columnCount();
    
    if (index.column() >= baseCols && (role == Qt::DisplayRole || role == Qt::EditRole)) {
        QString rowId = getRowId(index);
        PurchaseInformation info = PurchaseInvoiceManager::decode(rowId);
        
        if (index.column() == baseCols) {
            return info.countryCodeFrom;
        } else if (index.column() == baseCols + 1) {
            return info.countryCodeTo;
        } else if (index.column() == baseCols + 2) {
            if (!info.country_vatRate_vat.isEmpty()) {
                QString vatCountry = info.country_vatRate_vat.keys().first();
                if (!info.country_vatRate_vat[vatCountry].isEmpty()) {
                    double rateProp = info.country_vatRate_vat[vatCountry].keys().first().toDouble();
                    return QString("%1%").arg(rateProp * 100.0, 0, 'f', 2);
                }
            }
            return QString("");
        }
    }
    
    return AbstractBooksTable::data(index, role);
}
