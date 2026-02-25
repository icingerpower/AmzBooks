#include "ServiceSalesBooksTable.h"
#include "ServiceClientManager.h"
#include "VatResolver.h"
#include "orders/OrderManager.h"
#include "orders/Shipment.h"
#include "orders/InvoicingInfo.h"
#include "books/Activity.h"
#include "ExceptionWithTitleText.h"
#include <QDebug>
#include <QUuid>
#include "orders/Result.h"

const QString ServiceSalesBooksTable::CHANNEL_SALE{QObject::tr("Sale service")};

ServiceSalesBooksTable::ServiceSalesBooksTable(
        const BooksConnections *bookConnections
        , OrderManager *orderManager
        , const QDir &workingDir
        , QObject *parent)
    : AbstractBooksTable(bookConnections, workingDir, parent)
    , m_orderManager(orderManager)
{
    init();
}

void ServiceSalesBooksTable::createSale(const ServiceClientManager *clientManager, int clientRow,
                                        const QDate &date, double netAmount, const QString &currency,
                                        const QString &invoiceId, const QString &account,
                                        const VatResolver *vatResolver)
{
    if (!clientManager) return;

    QString clientName = clientManager->getClientName(clientRow);
    QString serviceLabel = clientManager->getServiceLabel(clientRow);
    QString country = clientManager->getCountry(clientRow);

    // Calculate payment date based on client's payment type
    QDate paymentDate = clientManager->calculatePaymentDate(clientRow, date);

    // 1. Generate Order ID — "Service-{Date}-{ClientName}"
    QString orderId = QString("Service-%1-%2").arg(date.toString("yyyyMMdd"), clientName);

    if (m_orderManager->containsOrder(orderId)) {
        ExceptionWithTitleText exception(tr("Order Exists"), tr("The order ID %1 already exists.").arg(orderId));
        exception.raise();
    }

    // 2. Compute VAT from the net amount using VatResolver
    double vatAmount = 0.0;
    if (vatResolver && vatResolver->hasRate(date, country, SaleType::Service)) {
        double vatRate = vatResolver->getRate(date, country, SaleType::Service);
        vatAmount = netAmount * vatRate;
    }
    double grossAmount = netAmount + vatAmount;

    // 3. Create Activity
    QString activityId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    Amount amountObj(netAmount, vatAmount);
    
    auto res = Activity::create(
        orderId,                    // Event ID
        activityId,                 // Activity ID
        serviceLabel,               // Sub ID (Storing Label here for persistence)
        date.startOfDay(),          // Date
        date.startOfDay(),          // dateTimeTax (Assuming same as date for now)
        currency,                   // Currency
        "FR",                       // Country From (Placeholder)
        country,                    // Country To
        false,                      // isCompany (manual service entry, defaulting to B2C)
        "",                         // Vat Paid To
        amountObj,
        TaxSource::ManualOverride,// Manual entry
        "",                         // Declaring Country
        TaxScheme::Unknown,         // Scheme
        TaxJurisdictionLevel::Unknown,
        SaleType::Service,          // SaleType::Service
        "", "",
        invoiceId                   // Invoice ID
    );

    if (!res.value) {
        QStringList errorMessages;
        for (const auto &err : res.errors) {
            errorMessages << err.message;
        }
        // Use ExceptionParamValue or standard exception for internal logic error?
        // ExceptionBookEquality was used before.
        ExceptionWithTitleText exception(tr("Error creating activity"), tr("Error creating activity: %1").arg(errorMessages.join(", ")));
        exception.raise();
    }
    
    QList<Activity> activities;
    activities.append(*res.value);
    Shipment shipment(activities);
    
    // 4. Record in OrderManager
    ActivitySource source;
    // Use API type as fallback for external/manual creation
    source.type = ActivitySourceType::API; 
    source.channel = CHANNEL_SALE;
    source.subchannel = "";
    source.reportOrMethode = ActivitySource::METHOD_USER_ENTRY;
    
    m_orderManager->recordShipmentFromSource(orderId, &source, &shipment, date, false);
    
    // 4. Create and record InvoicingInfo with payment date
    // Use paymentDate only if it differs from orderDate (non-instant payment)
    std::optional<QDate> optPaymentDate = (paymentDate != date) ? std::optional<QDate>(paymentDate) : std::nullopt;
    
    auto resInfo = InvoicingInfo::create(&shipment, {}, invoiceId, std::nullopt, optPaymentDate);
    if (resInfo.ok()) {
        m_orderManager->recordInvoicingInfo(activityId, &resInfo.value.value());
    } else {
        QString err = resInfo.errors.isEmpty() ? "Unknown" : resInfo.errors.first().message;
        qWarning() << "Failed to create InvoicingInfo for service sale:" << orderId << err;
    }
    
    // 5. Add to AbstractBooksTable (gross amount = net + vat)
    add(orderId
        , invoiceId
        , date
        , grossAmount
        , currency
        , serviceLabel
        , account  // Account 1
        , ""       // Account 2
        , vatAmount
        , country  // VAT Country
        , currency
    );
}

bool ServiceSalesBooksTable::remove(const QString &rowId)
{
    // 1. Remove from OrderManager
    m_orderManager->removeOrder(rowId);
    
    // 2. Remove from AbstractBooksTable
    return AbstractBooksTable::remove(rowId);
}

void ServiceSalesBooksTable::load(int year)
{
    if (!m_orderManager) {
        return;
    }

    QDate from(year, 1, 1);
    QDate to(year, 12, 31);

    // Lambda to accept only our ManualService activities
    auto filter = [](const ActivitySource* source, const Shipment*) {
        if (!source) return false;
        return source->type == ActivitySourceType::API
               && source->channel == CHANNEL_SALE
               && source->reportOrMethode == ActivitySource::METHOD_USER_ENTRY;
    };

    auto shipmentsMap = m_orderManager->getShipmentAndRefunds(from, to, filter);

    for (auto it = shipmentsMap.constBegin(); it != shipmentsMap.constEnd(); ++it) {
        const QSharedPointer<Shipment> &shipment = it.value();
        if (!shipment) continue;
        
        // Assuming one activity per shipment for service sales
        const QList<Activity> &activities = shipment->getActivities();
        if (activities.isEmpty()) continue;
        
        const Activity &act = activities.first();

        add(act.getEventId(), 
            act.getInvoiceId(), 
            act.getDateTime().date(), 
            act.getAmountTaxed() + act.getAmountTaxes(), // Total Amount
            act.getCurrency(), 
            act.getSubActivityId(), // Label stored in subActivityId
            "", "", 
            act.getAmountTaxes(), 
            act.getCountryCodeTo(), // Used as VAT Country
            act.getCurrency());
    }
}
