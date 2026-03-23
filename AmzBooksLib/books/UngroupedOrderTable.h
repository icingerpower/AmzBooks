#ifndef UNGROUPEDORDERTABLE_H
#define UNGROUPEDORDERTABLE_H

#include "AbstractBooksTable.h"

class OrderManager;
class BooksAccountsSalesTable;

class UngroupedOrderTable : public AbstractBooksTable
{
    Q_OBJECT

public:
    explicit UngroupedOrderTable(
            const BooksConnections *bookConnections,
            OrderManager *orderManager,
            const QDir &workingDir,
            const BooksAccountsSalesTable *salesTable = nullptr,
            const QString &companyCountry = QString(),
            const QString &companyCurrency = QString(),
            QObject *parent = nullptr);

    QString getId() const override;
    void load(int year) override;

private:
    OrderManager *m_orderManager;
    const BooksAccountsSalesTable *m_salesTable = nullptr;
    QString m_companyCountry;
    QString m_companyCurrency;
};

#endif // UNGROUPEDORDERTABLE_H
