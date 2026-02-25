#ifndef SERVICESALESBOOKSTABLE_H
#define SERVICESALESBOOKSTABLE_H

#include "AbstractBooksTable.h"
#include <QObject>

class ServiceClientManager;
class OrderManager;
class VatResolver;

class ServiceSalesBooksTable : public AbstractBooksTable
{
    Q_OBJECT

public:
    static const QString CHANNEL_SALE;
    explicit ServiceSalesBooksTable(
            const BooksConnections *bookConnections
            , OrderManager *orderManager
            , const QDir &workingDir
            , QObject *parent = nullptr);

    QString getId() const override { return "ServiceSales"; }

    // Custom Methods
    void createSale(const ServiceClientManager *clientManager, int clientRow,
                    const QDate &date, double netAmount, const QString &currency,
                    const QString &invoiceId, const QString &account,
                    const VatResolver *vatResolver = nullptr);
    void load(int year) override;

    bool remove(const QString &rowId) override;

private:
    OrderManager *m_orderManager;
};

#endif // SERVICESALESBOOKSTABLE_H
