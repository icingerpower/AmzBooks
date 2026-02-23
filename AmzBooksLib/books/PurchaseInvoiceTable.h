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

    void load(int year) override;

    PurchaseInvoiceManager &manager() const;

    // Remove invoice from table and manager (deletes file)
    void removeInvoice(const QModelIndex &index);

    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

private:
    PurchaseInvoiceManager *m_manager;
    QDir m_workingDir;
};

#endif // PURCHASEINVOICETABLE_H
