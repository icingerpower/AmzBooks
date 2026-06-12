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
                                       const QString &reference,
                                       const QString &title,
                                       bool vatOnPayment,
                                       const QString &paymentTerm)
{
    m_extraData[rowId] = QVariantList{reference, title, vatOnPayment, paymentTerm};
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
    return AbstractBooksTable::columnCount(parent) + 4;
}

QVariant ServiceSalesBooksTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        if (section == IND_REFERENCE)      return tr("Reference");
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
    if (col < IND_REFERENCE)
        return AbstractBooksTable::data(index, role);

    const QString rowId = getRowId(index);
    if (!m_extraData.contains(rowId)) return QVariant{};
    const QVariantList &extra = m_extraData[rowId];

    if (col == IND_REFERENCE) {
        return (role == Qt::DisplayRole || role == Qt::EditRole)
               ? extra[0] : QVariant{};
    }
    if (col == IND_TITLE) {
        return (role == Qt::DisplayRole || role == Qt::EditRole)
               ? extra[1] : QVariant{};
    }
    if (col == IND_VAT_ON_PAYMENT) {
        if (role == Qt::EditRole)   return extra[2].toBool();
        if (role == Qt::DisplayRole) return extra[2].toBool() ? tr("Yes") : tr("No");
        return QVariant{};
    }
    if (col == IND_PAYMENT_TERM) {
        return (role == Qt::DisplayRole || role == Qt::EditRole)
               ? extra[3] : QVariant{};
    }
    return QVariant{};
}

Qt::ItemFlags ServiceSalesBooksTable::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = AbstractBooksTable::flags(index);
    if (index.isValid() && index.column() >= IND_REFERENCE)
        f |= Qt::ItemIsEditable;
    return f;
}

bool ServiceSalesBooksTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole) return false;

    const int col = index.column();
    if (col < IND_REFERENCE) return false;

    const QString rowId = getRowId(index);
    if (!m_extraData.contains(rowId)) return false;

    auto info = m_orderManager->getInvoicingInfo(rowId);
    if (!info) return false;

    QVariantList &extra = m_extraData[rowId];

    if (col == IND_REFERENCE) {
        const QString newRef = value.toString();
        info->setReference(newRef);
        m_orderManager->recordInvoicingInfo(rowId, info.data());
        extra[0] = newRef;
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }
    if (col == IND_TITLE) {
        const QString newTitle = value.toString();
        info->setItemName(0, newTitle);
        m_orderManager->recordInvoicingInfo(rowId, info.data());
        extra[1] = newTitle;
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }
    if (col == IND_VAT_ON_PAYMENT) {
        const bool newVop = value.toBool();
        info->setVatOnPayment(newVop);
        m_orderManager->recordInvoicingInfo(rowId, info.data());
        extra[2] = newVop;
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
        extra[3] = term;
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }
    return false;
}

void ServiceSalesBooksTable::createSale(const ServiceClientManager *clientManager
                                        , int clientRow
                                        , const QDate &date
                                        , const QString &currency
                                        , const QString &orderId
                                        , const QString &account
                                        , const QList<SaleLineItemInput> &lineItems
                                        , const VatResolver &vatResolver
                                        , const TaxResolver &taxResolver
                                        , PaymentType paymentType
                                        , int paymentDays
                                        , bool vatOnPayment
                                        , const std::function<bool()> &onMissingVatRate)
{
    if (!clientManager || lineItems.isEmpty()) return;

    const QString &serviceLabel = clientManager->getServiceLabel(clientRow);

    // Total taxed amount = sum of (unitPrice × quantity) across all line items
    double taxedAmount = 0.0;
    for (const auto &item : lineItems)
        taxedAmount += item.unitPriceTaxed * item.quantity;
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

    // 5. Build and validate InvoicingInfo BEFORE any DB writes, so a validation
    //    failure leaves the database untouched (no orphaned shipment records).
    std::optional<QDate> optPaymentDate = (paymentDate != date) ? std::optional<QDate>(paymentDate) : std::nullopt;

    QList<LineItem> invoiceLineItems;
    for (const auto &item : lineItems) {
        auto lineItemRes = LineItem::create(QString{}, item.title, item.unitPriceTaxed, vatRate, item.quantity);
        if (!lineItemRes.ok()) {
            QString err = lineItemRes.errors.isEmpty() ? "Unknown" : lineItemRes.errors.first().message;
            ExceptionWithTitleText(tr("Invalid Line Item"), err).raise();
        }
        invoiceLineItems.append(lineItemRes.value.value());
    }
    auto resInfo = InvoicingInfo::create(&shipment, invoiceLineItems, std::nullopt, std::nullopt, optPaymentDate);
    if (!resInfo.ok()) {
        QString err = resInfo.errors.isEmpty() ? "Unknown" : resInfo.errors.first().message;
        ExceptionWithTitleText(tr("Invalid Invoicing Info"), err).raise();
    }
    resInfo.value->setVatOnPayment(vatOnPayment);
    resInfo.value->setReference(orderId);

    // 6. All validation passed — now write to the database.
    ActivitySource source;
    source.type = ActivitySourceType::API;
    source.channel = CHANNEL_SALE;
    source.subchannel = "";
    source.reportOrMethode = ActivitySource::METHOD_USER_ENTRY;

    m_orderManager->recordShipmentFromSource(orderId, &source, &shipment, date, false);
    m_orderManager->recordOrders({{orderId, OrderManager::OrderInfo{QString(), false, clientAccount}}});

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
    m_orderManager->recordInvoicingInfo(activityId, &resInfo.value.value());

    // 7. Add to AbstractBooksTable (gross amount = net + vat)
    // account1 = client receivable account; account2 = service revenue account
    add(orderId
        , orderId
        , date
        , taxedAmount
        , currency
        , serviceLabel
        , clientAccount  // Account 1: client receivable
        , account        // Account 2: service revenue account
        , vatAmount
        , country  // VAT Country
        , currency
    );

    // 8. Populate extra columns — title shows first item (or all joined if multiple)
    const QString displayTitle = lineItems.size() == 1
        ? lineItems.first().title
        : lineItems.first().title + tr(" (+%1 more)").arg(lineItems.size() - 1);
    _setExtra(orderId, orderId, displayTitle, vatOnPayment, _paymentTermStr(date, paymentDate));
}

QList<ServiceSalesBooksTable::SaleLineItemInput> ServiceSalesBooksTable::getLineItems(const QString &rowId) const
{
    QList<SaleLineItemInput> result;
    const auto info = m_orderManager->getInvoicingInfo(rowId);
    if (!info) {
        return result;
    }
    for (const auto &item : info->getItems()) {
        result.append({item.getName(), item.getAmountTaxed(), item.getQuantity()});
    }
    return result;
}

void ServiceSalesBooksTable::_createRefundEntry(const QString &rowId, const QString &refundOrderId)
{
    // Locate the original row in the in-memory table
    int origRow = -1;
    for (int i = 0; i < rowCount(); ++i) {
        if (getRowId(index(i, 0)) == rowId) {
            origRow = i;
            break;
        }
    }
    if (origRow == -1) {
        return;
    }

    const QDate   origDate      = getDate(origRow);
    const QString origLabel     = getLabel(origRow);
    const QString origCurrency  = getCurrency(origRow);
    const QString origAccount1  = getAccount1(origRow); // client receivable
    const QString origAccount2  = getAccount2(origRow); // service revenue

    // Retrieve the original shipment from OrderManager to access full Activity data
    auto filter = [&rowId](const ActivitySource *source, const Shipment *shipment) -> bool {
        if (!source) return false;
        if (source->type != ActivitySourceType::API) return false;
        if (source->channel != CHANNEL_SALE) return false;
        if (source->reportOrMethode != ActivitySource::METHOD_USER_ENTRY) return false;
        if (!shipment || shipment->getActivities().isEmpty()) return false;
        return shipment->getActivities().first().getEventId() == rowId;
    };
    const auto shipmentsMap = m_orderManager->getShipmentAndRefunds(origDate, origDate, filter);
    if (shipmentsMap.isEmpty()) {
        return;
    }
    const auto &origShipment = shipmentsMap.constBegin().value();
    if (!origShipment || origShipment->getActivities().isEmpty()) {
        return;
    }
    const Activity &origAct = origShipment->getActivities().first();

    // Build the negated Activity (credit note amounts)
    const Amount refundAmount(-origAct.getAmountTaxed(), -origAct.getAmountTaxes());
    auto refundRes = Activity::create(
        refundOrderId,
        refundOrderId,
        origAct.getSubActivityId(),
        origAct.getDateTime(),
        origAct.getDateTimeTax(),
        origAct.getCurrency(),
        origAct.getCountryCodeFrom(),
        origAct.getCountryCodeTo(),
        origAct.getIsCompany(),
        origAct.getCountryCodeVatPaidTo(),
        refundAmount,
        TaxSource::SelfComputed,
        origAct.getTaxDeclaringCountryCode(),
        origAct.getTaxScheme(),
        origAct.getTaxJurisdictionLevel(),
        SaleType::Service,
        origAct.getVatTerritoryFrom(),
        origAct.getVatTerritoryTo(),
        origAct.getInvoiceId()
    );
    if (!refundRes.value) {
        return;
    }
    refundRes.value->setTaxes(-origAct.getAmountTaxes());

    // Record the refund shipment in OrderManager
    QList<Activity> refundActivities;
    refundActivities.append(*refundRes.value);
    const QString clientAccount = origShipment->customerAccount();
    Shipment refundShipment(refundActivities, clientAccount, false);

    ActivitySource source;
    source.type             = ActivitySourceType::API;
    source.channel          = CHANNEL_SALE;
    source.subchannel       = "";
    source.reportOrMethode  = ActivitySource::METHOD_USER_ENTRY;

    m_orderManager->recordShipmentFromSource(refundOrderId, &source, &refundShipment, origDate, false);
    m_orderManager->recordOrders({{refundOrderId, OrderManager::OrderInfo{QString(), false, clientAccount}}});

    // Carry over the billing address
    const auto origAddress = m_orderManager->getAddressTo(rowId);
    if (origAddress) {
        m_orderManager->recordAddressesTo({{refundOrderId, *origAddress}});
    }

    // Build negated InvoicingInfo (line items with reversed sign) for the credit note
    const auto origInfo = m_orderManager->getInvoicingInfo(rowId);
    bool origVatOnPayment = false;
    QString origTitle;
    if (origInfo) {
        origVatOnPayment = origInfo->getVatOnPayment();
        const double vatRate = origAct.getVatRate();
        QList<LineItem> refundItems;
        for (const auto &item : origInfo->getItems()) {
            auto li = LineItem::create(item.getSku(), item.getName(),
                                       -item.getAmountTaxed(), vatRate,
                                       item.getQuantity());
            if (li.ok()) {
                refundItems.append(*li.value);
            }
        }
        if (!origInfo->getItems().isEmpty()) {
            origTitle = origInfo->getItems().first().getName();
        }
        auto resInfo = InvoicingInfo::create(nullptr, refundItems,
                                             std::nullopt, std::nullopt, std::nullopt);
        if (resInfo.ok()) {
            resInfo.value->setVatOnPayment(origVatOnPayment);
            resInfo.value->setReference(refundOrderId);
            m_orderManager->recordInvoicingInfo(refundOrderId, &resInfo.value.value());
        }
    }

    // Add to the in-memory table with negated amounts
    add(refundOrderId,
        refundOrderId,
        origDate,
        -origAct.getAmountTaxed(),
        origCurrency,
        origLabel,
        origAccount1,  // account1: client receivable
        origAccount2,  // account2: service revenue account
        -origAct.getAmountTaxes(),
        origAct.getCountryCodeTo(),
        origCurrency);

    _setExtra(refundOrderId, refundOrderId, origTitle, origVatOnPayment,
              ServiceClientManager::paymentTypeLabel(PaymentType::Instant));
}

void ServiceSalesBooksTable::replacePublishedSale(
        const QString &rowId,
        const ServiceClientManager *clientManager,
        int clientRow,
        const QDate &date,
        const QString &currency,
        const QString &newOrderId,
        const QString &account,
        const QList<SaleLineItemInput> &lineItems,
        const VatResolver &vatResolver,
        const TaxResolver &taxResolver,
        PaymentType paymentType,
        int paymentDays,
        bool vatOnPayment,
        const std::function<bool()> &onMissingVatRate)
{
    if (!m_orderManager->isOrderPublished(rowId)) {
        ExceptionWithTitleText ex(tr("Sale Not Published"),
                                  tr("The sale \"%1\" has not been published. "
                                     "Use replaceSale for unpublished sales.").arg(rowId));
        ex.raise();
    }

    const QString refundOrderId = rowId + QStringLiteral("-CREDIT");
    if (m_orderManager->containsOrder(refundOrderId)) {
        ExceptionWithTitleText ex(tr("Credit Note Exists"),
                                  tr("A credit note for sale \"%1\" already exists.").arg(rowId));
        ex.raise();
    }

    _createRefundEntry(rowId, refundOrderId);

    createSale(clientManager, clientRow, date, currency, newOrderId, account,
               lineItems, vatResolver, taxResolver, paymentType, paymentDays,
               vatOnPayment, onMissingVatRate);
}

void ServiceSalesBooksTable::replaceSale(const QString &rowId,
                                         const ServiceClientManager *clientManager,
                                         int clientRow,
                                         const QDate &date,
                                         const QString &currency,
                                         const QString &newOrderId,
                                         const QString &account,
                                         const QList<SaleLineItemInput> &lineItems,
                                         const VatResolver &vatResolver,
                                         const TaxResolver &taxResolver,
                                         PaymentType paymentType,
                                         int paymentDays,
                                         bool vatOnPayment,
                                         const std::function<bool()> &onMissingVatRate)
{
    if (m_orderManager->isOrderPublished(rowId)) {
        ExceptionWithTitleText ex(tr("Sale Cannot Be Edited"),
                                  tr("The sale \"%1\" has been published and cannot be modified.").arg(rowId));
        ex.raise();
    }

    remove(rowId);

    createSale(clientManager, clientRow, date, currency, newOrderId, account,
               lineItems, vatResolver, taxResolver, paymentType, paymentDays,
               vatOnPayment, onMissingVatRate);
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
            act.getInvoiceId(),          // bookId (invoice/account code stored here)
            orderDate,
            act.getAmountTaxed(),        // Total Amount (TTC = gross)
            act.getCurrency(),
            act.getSubActivityId(),      // Label stored in subActivityId
            shipment->customerAccount(), // account1: client receivable
            act.getInvoiceId(),          // account2: service revenue account
            act.getAmountTaxes(),
            act.getCountryCodeTo(),      // Used as VAT Country
            act.getCurrency());

        // Populate extra columns from persisted InvoicingInfo
        const QString rowId = act.getEventId();
        auto info = m_orderManager->getInvoicingInfo(rowId);
        QString reference;
        QString title;
        bool vatOnPayment = false;
        QDate payDate = orderDate;
        if (info) {
            reference = info->getReference();
            vatOnPayment = info->getVatOnPayment();
            payDate = info->getPaymentDate(orderDate);
            if (!info->getItems().isEmpty()) {
                title = info->getItems().first().getName();
            }
        }
        // Fall back to rowId if no reference was persisted (legacy data)
        if (reference.isEmpty()) {
            reference = rowId;
        }
        _setExtra(rowId, reference, title, vatOnPayment, _paymentTermStr(orderDate, payDate));
    }
}
