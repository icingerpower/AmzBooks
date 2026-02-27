#include "ServiceSalesBooksTable.h"
#include "ServiceClientManager.h"
#include "VatResolver.h"
#include "TaxResolver.h"
#include "orders/OrderManager.h"
#include "orders/Shipment.h"
#include "orders/InvoicingInfo.h"
#include "books/Activity.h"
#include "ExceptionWithTitleText.h"
#include <QUuid>
#include "orders/Result.h"


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

void ServiceSalesBooksTable::createSale(const ServiceClientManager *clientManager
                                        , int clientRow
                                        , const QDate &date
                                        , double taxedAmount
                                        , const QString &currency
                                        , const QString &orderId
                                        , const QString &serviceTitle
                                        , int quantity
                                        , const QString &account
                                        , const VatResolver &vatResolver
                                        , const TaxResolver &taxResolver
                                        , const std::function<bool()> &onMissingVatRate)
{
    if (!clientManager) return;

    const QString &clientName = clientManager->getClientName(clientRow);
    const QString &serviceLabel = clientManager->getServiceLabel(clientRow);
    const QString &country = clientManager->getCountry(clientRow);

    // Calculate payment date based on client's payment type
    const QDate &paymentDate = clientManager->calculatePaymentDate(clientRow, date);

    // 1. Generate Order ID — "Service-{Date}-{ClientName}"
    //QString orderId = QString("Service-%1-%2").arg(date.toString("yyyyMMdd"), clientName).replace(" ", "-");

    if (m_orderManager->containsOrder(orderId)) {
        ExceptionWithTitleText exception(tr("Order Exists"), tr("The order ID %1 already exists.").arg(orderId));
        exception.raise();
    }

    // 2. Compute VAT from the net amount using VatResolver
    double vatAmount = 0.0;
    if (!vatResolver.hasRate(date, country, SaleType::Service)) {
        bool resolved = onMissingVatRate && onMissingVatRate();
        if (!resolved || !vatResolver.hasRate(date, country, SaleType::Service)) {
            ExceptionWithTitleText e(tr("Missing VAT rate"),
                tr("No VAT rate found for country %1 on %2.")
                    .arg(country, date.toString("yyyy-MM-dd")));
            e.raise();
        }
    }
    double netAmount = taxedAmount;
    double vatRate = 0.;
    if (vatResolver.hasRate(date, country, SaleType::Service)) {
        vatRate = vatResolver.getRate(date, country, SaleType::Service);
        netAmount = taxedAmount / (1 + vatRate);
        vatAmount = netAmount * vatRate;
    }

    // 3. Resolve tax context
    static const QString countryFrom = "FR";
    auto taxCtx = taxResolver.getTaxContext(
        date.startOfDay(),
        countryFrom,
        country,
        SaleType::Service,
        true // B2B
    );
    const QString &declaringCountry     = taxCtx.taxDeclaringCountryCode;
    const TaxScheme taxScheme           = taxCtx.taxScheme;
    const TaxJurisdictionLevel taxJurisdictionLevel = taxCtx.taxJurisdictionLevel;
    const QString &vatPaidTo            = taxCtx.countryCodeVatPaidTo;

    // 4. Create Activity
    const QString &activityId = orderId;
    Amount amountObj(taxedAmount, vatAmount);

    auto res = Activity::create(
        orderId,                    // Event ID
        activityId,                 // Activity ID
        serviceLabel,               // Sub ID (Storing Label here for persistence)
        date.startOfDay(),          // Date
        date.startOfDay(),          // dateTimeTax (Assuming same as date for now)
        currency,                   // Currency
        countryFrom,                // Country From
        country,                    // Country To
        false,                      // isCompany (manual service entry, defaulting to B2C)
        vatPaidTo,                  // Vat Paid To
        amountObj,
        TaxSource::SelfComputed,  // Manual entry
        declaringCountry,           // Declaring Country
        taxScheme,                  // Scheme
        taxJurisdictionLevel,       // Jurisdiction Level
        SaleType::Service,          // SaleType::Service
        "", "",
        QString{}                   // Invoice ID
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

    // Activity::create() always initialises m_AmountTaxesComputed to 0.
    // For ManualOverride/SelfComputed sources getAmountTaxes() uses that field,
    // so set it explicitly to the VAT amount computed above.
    res.value->setTaxes(vatAmount);

    QList<Activity> activities;
    activities.append(*res.value);
    const QString clientAccount = clientManager->getAccount(clientRow);
    Shipment shipment(activities, clientAccount, false); // Service sales are not grouped

    // 5. Record in OrderManager
    ActivitySource source;
    // Use API type as fallback for external/manual creation
    source.type = ActivitySourceType::API;
    source.channel = CHANNEL_SALE;
    source.subchannel = "";
    source.reportOrMethode = ActivitySource::METHOD_USER_ENTRY;

    m_orderManager->recordShipmentFromSource(orderId, &source, &shipment, date, false);
    m_orderManager->recordOrders({{orderId, OrderManager::OrderInfo{QString(), false, clientAccount}}});

    // 6. Create and record InvoicingInfo with payment date
    // Use paymentDate only if it differs from orderDate (non-instant payment)
    std::optional<QDate> optPaymentDate = (paymentDate != date) ? std::optional<QDate>(paymentDate) : std::nullopt;
    
    double unitTaxedAmount = taxedAmount / quantity;
    auto lineItemRes = LineItem::create(QString{}, serviceTitle, unitTaxedAmount, vatRate, quantity);
    if (!lineItemRes.ok()) {
        QString err = lineItemRes.errors.isEmpty() ? "Unknown" : lineItemRes.errors.first().message;
        ExceptionWithTitleText(tr("Invalid Line Item"), err).raise();
    }
    auto resInfo = InvoicingInfo::create(&shipment, {lineItemRes.value.value()}, QString{}, std::nullopt, optPaymentDate);
    if (!resInfo.ok()) {
        QString err = resInfo.errors.isEmpty() ? "Unknown" : resInfo.errors.first().message;
        ExceptionWithTitleText(tr("Invalid Invoicing Info"), err).raise();
    }
    m_orderManager->recordInvoicingInfo(activityId, &resInfo.value.value());
    
    // 7. Add to AbstractBooksTable (gross amount = net + vat)
    add(orderId
        , orderId
        , date
        , taxedAmount
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
            act.getAmountTaxed(), // Total Amount (TTC = gross)
            act.getCurrency(), 
            act.getSubActivityId(), // Label stored in subActivityId
            "", "", 
            act.getAmountTaxes(), 
            act.getCountryCodeTo(), // Used as VAT Country
            act.getCurrency());
    }
}
