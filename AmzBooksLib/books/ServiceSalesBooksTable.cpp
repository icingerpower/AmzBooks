#include "ServiceSalesBooksTable.h"
#include "ServiceClientManager.h"
#include "VatResolver.h"
#include "TaxResolver.h"
#include "InvoiceGenerator.h"
#include "orders/OrderManager.h"
#include "orders/Shipment.h"
#include "orders/InvoicingInfo.h"
#include "orders/Address.h"
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

QString ServiceSalesBooksTable::getId() const
{
    return "ServiceSales";
}

// ---------------------------------------------------------------------------
// Extra-column helpers
// ---------------------------------------------------------------------------

void ServiceSalesBooksTable::_setExtra(const QString &rowId,
                                       const QString &title,
                                       bool vatOnPayment,
                                       const QString &paymentTerm)
{
    m_extraData[rowId] = QVariantList{title, vatOnPayment, paymentTerm};
}

/*static*/ QString ServiceSalesBooksTable::_paymentTermStr(const QDate &orderDate,
                                                            const QDate &paymentDate)
{
    if (paymentDate == orderDate)
        return ServiceClientManager::paymentTypeLabel(PaymentType::Instant);

    QDate nextMonth = orderDate.addMonths(1);
    QDate endOfNextMonth(nextMonth.year(), nextMonth.month(), nextMonth.daysInMonth());
    if (paymentDate == endOfNextMonth)
        return ServiceClientManager::paymentTypeLabel(PaymentType::EndOfNextMonth);

    int days = orderDate.daysTo(paymentDate);
    return QString("After %1 days").arg(days);
}

// ---------------------------------------------------------------------------
// QAbstractTableModel overrides for the 3 extra columns
// ---------------------------------------------------------------------------

int ServiceSalesBooksTable::columnCount(const QModelIndex &parent) const
{
    return AbstractBooksTable::columnCount(parent) + 3;
}

QVariant ServiceSalesBooksTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        if (section == IND_TITLE)          return tr("Title");
        if (section == IND_VAT_ON_PAYMENT) return tr("VAT on Payment");
        if (section == IND_PAYMENT_TERM)   return tr("Payment Term");
    }
    return AbstractBooksTable::headerData(section, orientation, role);
}

QVariant ServiceSalesBooksTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant{};

    const int col = index.column();
    if (col < IND_TITLE)
        return AbstractBooksTable::data(index, role);

    const QString rowId = getRowId(index);
    if (!m_extraData.contains(rowId)) return QVariant{};
    const QVariantList &extra = m_extraData[rowId];

    if (col == IND_TITLE) {
        return (role == Qt::DisplayRole || role == Qt::EditRole)
               ? extra[0] : QVariant{};
    }
    if (col == IND_VAT_ON_PAYMENT) {
        if (role == Qt::EditRole)   return extra[1].toBool();
        if (role == Qt::DisplayRole) return extra[1].toBool() ? tr("Yes") : tr("No");
        return QVariant{};
    }
    if (col == IND_PAYMENT_TERM) {
        return (role == Qt::DisplayRole || role == Qt::EditRole)
               ? extra[2] : QVariant{};
    }
    return QVariant{};
}

Qt::ItemFlags ServiceSalesBooksTable::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = AbstractBooksTable::flags(index);
    if (index.isValid() && index.column() >= IND_TITLE)
        f |= Qt::ItemIsEditable;
    return f;
}

bool ServiceSalesBooksTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole) return false;

    const int col = index.column();
    if (col < IND_TITLE) return false;

    const QString rowId = getRowId(index);
    if (!m_extraData.contains(rowId)) return false;

    auto info = m_orderManager->getInvoicingInfo(rowId);
    if (!info) return false;

    QVariantList &extra = m_extraData[rowId];

    if (col == IND_TITLE) {
        const QString newTitle = value.toString();
        info->setItemName(0, newTitle);
        m_orderManager->recordInvoicingInfo(rowId, info.data());
        extra[0] = newTitle;
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }
    if (col == IND_VAT_ON_PAYMENT) {
        const bool newVop = value.toBool();
        info->setVatOnPayment(newVop);
        m_orderManager->recordInvoicingInfo(rowId, info.data());
        extra[1] = newVop;
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }
    if (col == IND_PAYMENT_TERM) {
        const QString term = value.toString();
        const QDate orderDate = getDate(index.row());
        std::optional<QDate> newPayDate;
        if (term == ServiceClientManager::paymentTypeLabel(PaymentType::Instant)) {
            newPayDate = std::nullopt;
        } else if (term == ServiceClientManager::paymentTypeLabel(PaymentType::EndOfNextMonth)) {
            QDate nm = orderDate.addMonths(1);
            newPayDate = QDate(nm.year(), nm.month(), nm.daysInMonth());
        } else if (term.startsWith(QStringLiteral("After ")) && term.endsWith(QStringLiteral(" days"))) {
            const QString daysStr = term.mid(6, term.length() - 11);
            bool ok = false;
            const int days = daysStr.toInt(&ok);
            if (!ok || days <= 0) return false;
            newPayDate = orderDate.addDays(days);
        } else {
            return false;
        }
        info->setPaymentDate(newPayDate);
        m_orderManager->recordInvoicingInfo(rowId, info.data());
        extra[2] = term;
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }
    return false;
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
                                        , PaymentType paymentType
                                        , int paymentDays
                                        , bool vatOnPayment
                                        , const std::function<bool()> &onMissingVatRate)
{
    if (!clientManager) return;

    const QString &clientName = clientManager->getClientName(clientRow);
    const QString &serviceLabel = clientManager->getServiceLabel(clientRow);
    const QString &country = clientManager->getCountry(clientRow);

    // Calculate payment date from the explicit payment term
    QDate paymentDate;
    switch (paymentType) {
    case PaymentType::Instant:
        paymentDate = date;
        break;
    case PaymentType::AfterXDays:
        paymentDate = date.addDays(paymentDays);
        break;
    case PaymentType::EndOfNextMonth: {
        QDate nextMonth = date.addMonths(1);
        paymentDate = QDate(nextMonth.year(), nextMonth.month(), nextMonth.daysInMonth());
        break;
    }
    }

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
        account                     // Store bookkeeping account in invoiceId for persistence
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

    // Record client address so the invoice can display the destination
    Address clientAddress(
        clientManager->getClientName(clientRow),    // fullName
        clientManager->getStreet1(clientRow),        // addressLine1
        clientManager->getStreet2(clientRow),        // addressLine2
        QString(),                                   // addressLine3
        clientManager->getCity(clientRow),           // city
        clientManager->getPostalCode(clientRow),     // postalCode
        country,                                     // countryCode (already fetched above)
        QString(),                                   // stateOrRegion
        QString(),                                   // email
        QString(),                                   // phone
        clientManager->getClientName(clientRow),    // companyName
        clientManager->getVatNumber(clientRow)       // taxId
    );
    m_orderManager->recordAddressesTo({{orderId, clientAddress}});

    // 6. Create and record InvoicingInfo with payment date
    // Use paymentDate only if it differs from orderDate (non-instant payment)
    std::optional<QDate> optPaymentDate = (paymentDate != date) ? std::optional<QDate>(paymentDate) : std::nullopt;
    
    double unitTaxedAmount = taxedAmount / quantity;
    auto lineItemRes = LineItem::create(QString{}, serviceTitle, unitTaxedAmount, vatRate, quantity);
    if (!lineItemRes.ok()) {
        QString err = lineItemRes.errors.isEmpty() ? "Unknown" : lineItemRes.errors.first().message;
        ExceptionWithTitleText(tr("Invalid Line Item"), err).raise();
    }
    auto resInfo = InvoicingInfo::create(&shipment, {lineItemRes.value.value()}, std::nullopt, std::nullopt, optPaymentDate);
    if (!resInfo.ok()) {
        QString err = resInfo.errors.isEmpty() ? "Unknown" : resInfo.errors.first().message;
        ExceptionWithTitleText(tr("Invalid Invoicing Info"), err).raise();
    }
    resInfo.value->setVatOnPayment(vatOnPayment);
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

    // 8. Populate extra columns (title, vatOnPayment, paymentTerm)
    _setExtra(orderId, serviceTitle, vatOnPayment, _paymentTermStr(date, paymentDate));
}

bool ServiceSalesBooksTable::remove(const QString &rowId)
{
    // 1. Remove the invoice registry entry BEFORE removeOrder deletes invoicing_infos
    if (m_invoiceGenerator) {
        auto info = m_orderManager->getInvoicingInfo(rowId);
        if (info) {
            const QString inv = info->getInvoiceNumber().value_or("");
            if (!inv.isEmpty()) {
                m_invoiceGenerator->removeInvoiceByNumber(inv);
            }
        }
    }

    // 2. Remove from OrderManager
    m_orderManager->removeOrder(rowId);

    // 3. Remove from AbstractBooksTable and extra data cache
    m_extraData.remove(rowId);
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
        const QDate orderDate = act.getDateTime().date();

        add(act.getEventId(),
            act.getInvoiceId(),         // bookId (invoice/account code stored here)
            orderDate,
            act.getAmountTaxed(), // Total Amount (TTC = gross)
            act.getCurrency(),
            act.getSubActivityId(), // Label stored in subActivityId
            act.getInvoiceId(),     // account1: recovered from invoiceId field
            "",
            act.getAmountTaxes(),
            act.getCountryCodeTo(), // Used as VAT Country
            act.getCurrency());

        // Populate extra columns from persisted InvoicingInfo
        const QString rowId = act.getEventId();
        auto info = m_orderManager->getInvoicingInfo(rowId);
        QString title;
        bool vatOnPayment = false;
        QDate payDate = orderDate;
        if (info) {
            vatOnPayment = info->getVatOnPayment();
            payDate = info->getPaymentDate(orderDate);
            if (!info->getItems().isEmpty())
                title = info->getItems().first().getName();
        }
        _setExtra(rowId, title, vatOnPayment, _paymentTermStr(orderDate, payDate));
    }
}
