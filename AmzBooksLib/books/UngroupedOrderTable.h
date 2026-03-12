#ifndef UNGROUPEDORDERTABLE_H
#define UNGROUPEDORDERTABLE_H

#include "AbstractBooksTable.h"

class OrderManager;

class UngroupedOrderTable : public AbstractBooksTable
{
    Q_OBJECT

public:
    explicit UngroupedOrderTable(
            const BooksConnections *bookConnections,
            OrderManager *orderManager,
            const QDir &workingDir,
            QObject *parent = nullptr);

    QString getId() const override;
    void load(int year) override;

private:
    OrderManager *m_orderManager;
};

#endif // UNGROUPEDORDERTABLE_H
