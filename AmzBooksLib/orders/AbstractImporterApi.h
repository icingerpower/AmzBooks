#ifndef ABSTRACTIMPORTERAPI_H
#define ABSTRACTIMPORTERAPI_H


#include <QString>
#include <QVariant>
#include <QMap>
#include <QDateTime>
#include <QVector>
#include <QStringList>

#include <functional>
#include <utility>

#include <QCoroTask>

#include "AbstractImporter.h"

// Generic parameter meta

class AbstractImporterApi : public AbstractImporter
{
public:
    QPair<QDateTime, QDateTime> datesFromToShipments() const; // Return invalid date if no data was every retrieved (saved in _settings())
    QPair<QDateTime, QDateTime> datesFromToRefunds() const; // Return invalid date if no data was every retrieved (saved in _settings())
    QPair<QDateTime, QDateTime> datesFromToAddresses() const; // Return invalid date if no data was every retrieved (saved in _settings())
    QPair<QDateTime, QDateTime> datesFromToInvoiceInfos() const; // Return invalid date if no data was every retrieved (saved in _settings())

    // API pulls (async)
    // One method can return all of one type depending on API returns
    QCoro::Task<ReturnOrderInfos> fetchShipments(const QDateTime &dateFrom); // Will call the virtual method and save last date done if successful (error empty)
    QCoro::Task<ReturnOrderInfos> fetchRefunds(const QDateTime &dateFrom);
    QCoro::Task<ReturnOrderInfos> fetchAddresses(const QDateTime &dateFrom);
    QCoro::Task<ReturnOrderInfos> fetchInvoiceInfos(const QDateTime &dateFrom);

protected:
    virtual QCoro::Task<ReturnOrderInfos> _fetchShipments(const QDateTime &dateFrom) = 0;
    virtual QCoro::Task<ReturnOrderInfos> _fetchRefunds(const QDateTime &dateFrom) = 0;
    virtual QCoro::Task<ReturnOrderInfos> _fetchAddresses(const QDateTime &dateFrom) = 0;
    virtual QCoro::Task<ReturnOrderInfos> _fetchInvoiceInfos(const QDateTime &dateFrom) = 0;
};
//*/

#endif // ABSTRACTIMPORTERAPI_H
