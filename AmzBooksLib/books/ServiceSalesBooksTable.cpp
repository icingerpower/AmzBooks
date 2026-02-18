#include "ServiceSalesBooksTable.h"
#include "ServiceClientManager.h"
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
                                        const QDate &date, double amount, const QString &currency, const QString &invoiceId)
{
    if (!clientManager) return;

    QString clientName = clientManager->getClientName(clientRow);
    QString serviceLabel = clientManager->getServiceLabel(clientRow);
    QString country = clientManager->getCountry(clientRow);
    
    // Calculate payment date based on client's payment type
    QDate paymentDate = clientManager->calculatePaymentDate(clientRow, date);

    // 1. Generate Order ID
    // Format: "Service-{Date}-{ClientName}"
    QString orderId = QString("Service-%1-%2").arg(date.toString("yyyyMMdd"), clientName);
    
    // Check existence
    if (m_orderManager->containsOrder(orderId)) {
        ExceptionWithTitleText exception(tr("Order Exists"), tr("The order ID %1 already exists.").arg(orderId));
        exception.raise();
    }

    // 2. Create Shipment / Activity
    // We need a unique Activity ID.
    QString activityId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    // Determine VAT
    // I will assume 0.0 VAT for now, as services often are Reverse Charge or Exempt.
    double vatAmount = 0.0;
    Amount amountObj(amount, vatAmount); // Net = Amount, Tax = 0
    
    auto res = Activity::create(
        orderId,                    // Event ID
        activityId,                 // Activity ID
        serviceLabel,               // Sub ID (Storing Label here for persistence)
        date.startOfDay(),          // Date
        currency,                   // Currency
        "FR",                       // Country From (Placeholder)
        country,                    // Country To
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
    
    // 3. Record in OrderManager
    ActivitySource source;
    // Use API type as fallback for external/manual creation
    source.type = ActivitySourceType::API; 
    source.channel = CHANNEL_SALE;
    source.subchannel = "";
    source.reportOrMethode = ActivitySource::METHOD_USER_ENTRY;
    
    m_orderManager->recordShipmentFromSource(orderId, &source, &shipment, date);
    
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
    
    // 5. Add to AbstractBooksTable
    // add(rowId, bookId, date, amountFullOrig, currencyAmount, label, account1, account2, vatOrig, vatCountry, vatCurrency)
    add(orderId, invoiceId, date, amount, currency, serviceLabel, 
        "", // Account 1 
        "", // Account 2
        vatAmount, 
        country, // Vat Country
        currency
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
