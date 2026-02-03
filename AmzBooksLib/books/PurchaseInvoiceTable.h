#ifndef PURCHASEINVOICETABLE_H
#define PURCHASEINVOICETABLE_H

#include "AbstractBooksTable.h"
#include "PurchaseInvoiceManager.h"

class PurchaseInvoiceTable : public AbstractBooksTable
{
    Q_OBJECT

public:
    explicit PurchaseInvoiceTable(
            const BooksConnections *bookConnections,
            const QDir &workingDir,
            QObject *parent = nullptr);

    virtual QString getId() const override;

    void load(int year);

    PurchaseInvoiceManager &manager() const;

    // Remove invoice from table and manager (deletes file)
    void removeInvoice(const QModelIndex &index);

private:
    PurchaseInvoiceManager *m_manager;
    QDir m_workingDir;
};

#endif // PURCHASEINVOICETABLE_H
