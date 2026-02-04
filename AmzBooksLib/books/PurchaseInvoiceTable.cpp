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
    clear();
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
            QString(), // Account 2
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
