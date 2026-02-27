#ifndef SERVICESALESBOOKSTABLE_H
#define SERVICESALESBOOKSTABLE_H

#include "AbstractBooksTable.h"
#include <QObject>
#include <functional>

class ServiceClientManager;
class OrderManager;
class VatResolver;
class TaxResolver;

class ServiceSalesBooksTable : public AbstractBooksTable
{
    Q_OBJECT

public:
    static constexpr QLatin1StringView CHANNEL_SALE{"Sale service"};
    explicit ServiceSalesBooksTable(
            const BooksConnections *bookConnections
            , OrderManager *orderManager
            , const QDir &workingDir
            , QObject *parent = nullptr);

    QString getId() const override { return "ServiceSales"; }

    // Custom Methods
    void createSale(const ServiceClientManager *clientManager
                    , int clientRow
                    , const QDate &date
                    , double taxedAmount
                    , const QString &currency
                    , const QString &orderId
                    , const QString &serviceTitle, int quantity, const QString &account
                    , const VatResolver &vatResolver
                    , const TaxResolver &taxResolver
                    , const std::function<bool()> &onMissingVatRate = nullptr);
    void load(int year) override;

    bool remove(const QString &rowId) override;

private:
    OrderManager *m_orderManager;
};

#endif // SERVICESALESBOOKSTABLE_H
