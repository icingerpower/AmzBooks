#include "ImporterFileCommerceHQ.h"
#include "CountriesEu.h"
#include "books/Activity.h"
#include "orders/Shipment.h"
#include "orders/Refund.h"
#include "orders/Address.h"
#include "orders/InvoicingInfo.h"
#include "orders/LineItem.h"
#include "utils/CsvReader.h"
#include "utils/CsvHeader.h"
#include <QFileInfo>
#include <QDebug>
#include <QLocale>

DECLARE_IMPORTER_FILE(ImporterFileCommerceHQ)

QString ImporterFileCommerceHQ::getLabel() const
{
    return QObject::tr("CommerceHQ Orders Report");
}

ActivitySource ImporterFileCommerceHQ::getActivitySource() const
{
    ActivitySource s;
    s.type = ActivitySourceType::Report;
    s.channel = "CommerceHQ";
    s.reportOrMethode = QObject::tr("CommerceHQ Orders Export");
    return s;
}

QString ImporterFileCommerceHQ::getId() const
{
    return "CommerceHQOrders";
}

QMap<QString, AbstractImporter::ParamInfo> ImporterFileCommerceHQ::getRequiredParams() const
{
    return {};
}

QString ImporterFileCommerceHQ::getUniqueReportId(const QString &filePath) const
{
    return QFileInfo(filePath).fileName();
}

bool ImporterFileCommerceHQ::recomputeTaxes() const
{
    return true;
}

bool ImporterFileCommerceHQ::isWrongIfConflict() const
{
    return true;
}

bool ImporterFileCommerceHQ::fixRefundDate() const
{
    return true;
}

bool ImporterFileCommerceHQ::isGroupedOrders() const
{
    return false;
}

double ImporterFileCommerceHQ::parseAmount(const QString &str)
{
    QString cleaned = str.trimmed();
    cleaned.remove('$');
    cleaned.remove(QChar(0x20AC)); // €
    cleaned.remove("USD").remove("EUR").remove("GBP");
    cleaned = cleaned.trimmed();
    if (cleaned.isEmpty())
        return 0.0;
    bool ok;
    double val = cleaned.toDouble(&ok);
    if (!ok)
    {
        qWarning() << "Failed to parse amount:" << str;
        return 0.0;
    }
    return val;
}

QDate ImporterFileCommerceHQ::parseCommerceHQDate(const QString &dateStr)
{
    // Format: MM/DD/YYYY (e.g. "01/18/2026")
    return AbstractImporterFile::parseDateFormats(
        dateStr.trimmed(), {"MM/dd/yyyy", "M/d/yyyy", "MM/d/yyyy", "M/dd/yyyy"});
}

QDateTime ImporterFileCommerceHQ::parseCommerceHQDateTime(const QString &dateStr, const QString &timeStr)
{
    QDate date = parseCommerceHQDate(dateStr);
    if (!date.isValid())
        return QDateTime();

    if (timeStr.trimmed().isEmpty())
        return QDateTime(date, QTime(0, 0, 0));

    // Format: "10:40pm" or "9:05am" — normalize to uppercase for Qt parsing
    QString cleanTime = timeStr.trimmed().toUpper();

    QLocale enLocale(QLocale::English);
    QTime time;
    for (const QString &fmt : {"h:mmAP", "hh:mmAP"})
    {
        time = enLocale.toTime(cleanTime, fmt);
        if (time.isValid())
            break;
    }

    if (!time.isValid())
    {
        qWarning() << "Failed to parse time:" << timeStr << "— using midnight";
        return QDateTime(date, QTime(0, 0, 0));
    }

    return QDateTime(date, time);
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterFileCommerceHQ::_loadReport(
    const QString &filePath,
    std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing)
{
    Q_UNUSED(callbackAddIfMissing);

    AbstractImporter::ReturnOrderInfos ret;
    ret.orderInfos = QSharedPointer<AbstractImporter::OrderInfos>::create();

    CsvReader reader(filePath, ",", "\"", true, "\n", 0, "UTF-8");
    if (!reader.readAll())
    {
        ret.errorReturned = "Could not read CSV file: " + filePath;
        co_return ret;
    }
    const auto *csvData = reader.dataRode();

    // Required columns — CsvHeader::pos() throws CsvHeaderException if missing
    int idxOrderNumber = csvData->header.pos("order-number");
    int idxOrderDate   = csvData->header.pos("order-date");
    int idxSubtotal    = csvData->header.pos("subtotal-paid");
    int idxTax         = csvData->header.pos("tax");
    int idxCountryCode = csvData->header.pos("country-code");

    // Optional columns
    int idxOrderTime  = csvData->header.contains("order-time")      ? csvData->header.pos("order-time")      : -1;
    int idxRefunded   = csvData->header.contains("refunded-amount") ? csvData->header.pos("refunded-amount") : -1;
    int idxRefundDate = csvData->header.contains("refund-date")     ? csvData->header.pos("refund-date")     : -1;
    // Line-item columns (optional — used to build InvoicingInfo for invoice generation)
    int idxSku          = csvData->header.contains("sku")            ? csvData->header.pos("sku")            : -1;
    int idxProductTitle = csvData->header.contains("product-title")  ? csvData->header.pos("product-title")  : -1;
    int idxQuantity     = csvData->header.contains("quantity")        ? csvData->header.pos("quantity")        : -1;

    // Address columns (all optional — absence degrades gracefully to empty strings)
    int idxFullName   = csvData->header.contains("full-name")       ? csvData->header.pos("full-name")       : -1;
    int idxStreetAddr = csvData->header.contains("street-address")  ? csvData->header.pos("street-address")  : -1;
    int idxAddrLine2  = csvData->header.contains("address-line-2")  ? csvData->header.pos("address-line-2")  : -1;
    int idxCity       = csvData->header.contains("city")            ? csvData->header.pos("city")            : -1;
    int idxZip        = csvData->header.contains("zip-code")        ? csvData->header.pos("zip-code")        : -1;
    int idxState      = csvData->header.contains("state")           ? csvData->header.pos("state")           : -1;
    int idxEmail      = csvData->header.contains("email")           ? csvData->header.pos("email")           : -1;
    int idxPhone      = csvData->header.contains("phone")           ? csvData->header.pos("phone")           : -1;

    // Seller origin country and billing currency (CommerceHQ stores are configured by the seller)
    static const QString originCountry = "FR";
    static const QString currency = "USD";

    // Per-order accumulator: amounts from multiple SKU rows are summed into one shipment.
    struct LineItemAccum {
        QString sku;
        QString name;
        double  subtotal = 0.0; // untaxed amount for this row (subtotal-paid column)
        int     quantity = 1;
    };
    struct OrderAccum {
        QDateTime firstDt;
        double    subtotalSum    = 0.0;
        double    taxSum         = 0.0;
        double    refundedAmount = 0.0; // taken from first row that carries a non-zero value
        QDate     refundDate;
        QString   destCountry;
        // Per-row line items — used to build InvoicingInfo for invoice generation
        QList<LineItemAccum> lineItems;
        // Address fields captured from the first row of this order
        QString   fullName, streetAddr, addrLine2, city, zip, state, email, phone;
    };

    QList<QString>             orderKeys; // insertion-ordered unique order numbers
    QHash<QString, OrderAccum> orderMap;

    // First pass — accumulate amounts and capture first-row metadata per order
    for (const auto &line : csvData->lines)
    {
        if (line.isEmpty())
            continue;

        const QString orderNumber = line.value(idxOrderNumber).trimmed();
        if (orderNumber.isEmpty())
            continue;

        // Validate date (required on every row)
        const QString dateStr = line.value(idxOrderDate).trimmed();
        const QString timeStr = (idxOrderTime != -1) ? line.value(idxOrderTime).trimmed() : "";
        const QDateTime dt    = parseCommerceHQDateTime(dateStr, timeStr);
        if (!dt.isValid())
        {
            ret.errorReturned = "Invalid date format: " + dateStr;
            co_return ret;
        }

        const bool isNewOrder = !orderMap.contains(orderNumber);
        if (isNewOrder)
        {
            orderKeys.append(orderNumber);
            auto col = [&](int idx) -> QString {
                return (idx != -1) ? line.value(idx).trimmed() : QString{};
            };
            OrderAccum &acc  = orderMap[orderNumber];
            acc.firstDt      = dt;
            acc.destCountry  = line.value(idxCountryCode).trimmed().toUpper();
            if (acc.destCountry.isEmpty())
                acc.destCountry = "US";
            acc.fullName   = col(idxFullName);
            acc.streetAddr = col(idxStreetAddr);
            acc.addrLine2  = col(idxAddrLine2);
            acc.city       = col(idxCity);
            acc.zip        = col(idxZip);
            acc.state      = col(idxState);
            acc.email      = col(idxEmail);
            acc.phone      = col(idxPhone);
        }

        OrderAccum &acc  = orderMap[orderNumber];
        const double rowSubtotal = parseAmount(line.value(idxSubtotal));
        acc.subtotalSum += rowSubtotal;
        acc.taxSum      += parseAmount(line.value(idxTax));

        // Accumulate per-row line item data for InvoicingInfo construction
        {
            LineItemAccum li;
            li.sku      = (idxSku          != -1) ? line.value(idxSku).trimmed()          : QString{};
            li.name     = (idxProductTitle != -1) ? line.value(idxProductTitle).trimmed() : QString{};
            li.subtotal = rowSubtotal;
            li.quantity = (idxQuantity != -1) ? qMax(1, line.value(idxQuantity).toInt()) : 1;
            acc.lineItems.append(li);
        }

        // Refund fields: capture from the first row that carries a non-zero refunded-amount
        if (idxRefunded != -1 && acc.refundedAmount == 0.0)
        {
            const double r = parseAmount(line.value(idxRefunded));
            if (r > 0.0)
            {
                acc.refundedAmount = r;
                if (idxRefundDate != -1)
                    acc.refundDate = parseCommerceHQDate(line.value(idxRefundDate).trimmed());
            }
        }
    }

    // Second pass — one Shipment (and optionally one Refund or refundClue) per order
    for (const auto &orderNumber : std::as_const(orderKeys))
    {
        const OrderAccum &acc = orderMap[orderNumber];

        // Determine tax scheme based on seller (FR) → buyer geography
        TaxScheme scheme;
        if (acc.destCountry == originCountry)
            scheme = TaxScheme::DomesticVat;
        else if (CountriesEu::isEuMember(acc.destCountry, acc.firstDt.date()))
            scheme = TaxScheme::EuOssUnion;
        else
            scheme = TaxScheme::Exempt; // Export outside EU

        ::Amount amount(acc.subtotalSum + acc.taxSum, acc.taxSum);

        auto actResult = Activity::create(
            orderNumber,                    // eventId
            orderNumber,                    // activityId (one per order)
            "",                             // subActivityId
            acc.firstDt,
            acc.firstDt,
            currency,
            originCountry,
            acc.destCountry,
            false,                          // isCompany (B2C store)
            acc.destCountry,
            amount,
            TaxSource::MarketplaceProvided,
            originCountry,
            scheme,
            TaxJurisdictionLevel::Country,
            SaleType::Products
        );

        if (!actResult.ok())
        {
            ret.errorReturned = "Activity create error for order: " + orderNumber;
            co_return ret;
        }

        QList<Activity> acts;
        acts.append(*actResult.value);
        Shipment shipment(acts, "", isGroupedOrders());
        ret.orderInfos->shipments.append(shipment);

        // Build InvoicingInfo for this shipment so that generateInvoices() can produce a PDF.
        // vatRate is derived from the order totals and applied uniformly to all rows.
        // Use qAbs() throughout: some CommerceHQ exports carry negative subtotal-paid for
        // refunded orders, which would otherwise invert the sign of the invoice line items.
        {
            const double absSubtotalSum = qAbs(acc.subtotalSum);
            const double vatRate = (absSubtotalSum > 0.0001)
                ? (qAbs(acc.taxSum) / absSubtotalSum) : 0.0;

            QList<LineItem> lineItems;
            for (const auto &li : std::as_const(acc.lineItems))
            {
                if (li.subtotal == 0.0 || li.quantity <= 0)
                    continue;
                const double taxedPerUnit = qAbs(li.subtotal) * (1.0 + vatRate) / li.quantity;
                const QString name = !li.name.isEmpty() ? li.name
                                   : (!li.sku.isEmpty() ? li.sku : QObject::tr("Products"));
                auto liRes = LineItem::create(li.sku, name, taxedPerUnit, vatRate, li.quantity);
                if (liRes.ok())
                    lineItems.append(*liRes.value);
            }
            // Fall back to one aggregate line item if all per-row entries were zero
            if (lineItems.isEmpty())
            {
                const double total = qAbs(acc.subtotalSum + acc.taxSum);
                if (total != 0.0)
                {
                    auto liRes = LineItem::create("", QObject::tr("Products"), total, vatRate, 1);
                    if (liRes.ok())
                        lineItems.append(*liRes.value);
                }
            }
            if (!lineItems.isEmpty())
            {
                auto infoRes = InvoicingInfo::create(
                    &ret.orderInfos->shipments.last(),
                    lineItems, std::nullopt, std::nullopt, std::nullopt);
                if (infoRes.ok())
                {
                    ret.orderInfos->invoicingInfos.append(
                        InvoicingInfoWithId{ret.orderInfos->shipments.last().getId(),
                                            *infoRes.value});
                }
                else
                {
                    qWarning() << "ImporterFileCommerceHQ: InvoicingInfo failed for order"
                               << orderNumber;
                }
            }
        }

        // Delivery address (one per order)
        Address addr(
            acc.fullName, acc.streetAddr, acc.addrLine2,
            {},             // addressLine3 (not in CSV)
            acc.city, acc.zip, acc.destCountry, acc.state,
            acc.email, acc.phone,
            {},             // companyName (not in CSV)
            {}              // taxId (not in CSV)
        );
        ret.orderInfos->orderAddresses.append(AddressToWithId{orderNumber, addr});

        // Register order grouping — isGroupedOrders() = false for CommerceHQ so that
        // UngroupedOrderTable and the grouped/ungrouped filters work correctly.
        ret.orderInfos->orderId_infos[orderNumber] =
            OrderManager::OrderInfo{"CommerceHQ", isGroupedOrders(), ""};

        // Refund handling: full refund → Refund entry; partial → orderId_refundClue
        if (acc.refundedAmount > 0.0)
        {
            const double totalGross  = qAbs(acc.subtotalSum + acc.taxSum);
            const bool isFullRefund  = totalGross > 0.001 && qAbs(acc.refundedAmount - totalGross) < 0.01;

            if (isFullRefund)
            {
                const QDate refundDate = acc.refundDate.isValid() ? acc.refundDate : acc.firstDt.date();
                QDateTime refundDt(refundDate, QTime(0, 0, 0));

                ::Amount refundAmount(-acc.refundedAmount, -acc.taxSum);

                auto refundActResult = Activity::create(
                    orderNumber,
                    orderNumber + "_refund",
                    "",
                    refundDt,
                    refundDt,
                    currency,
                    originCountry,
                    acc.destCountry,
                    false,
                    acc.destCountry,
                    refundAmount,
                    TaxSource::MarketplaceProvided,
                    originCountry,
                    scheme,
                    TaxJurisdictionLevel::Country,
                    SaleType::Products
                );

                if (refundActResult.ok())
                {
                    QList<Activity> refundActs;
                    refundActs.append(*refundActResult.value);
                    Refund refund(refundActs, "", isGroupedOrders());
                    ret.orderInfos->refunds.append(refund);

                    // Build InvoicingInfo for the refund.
                    // Negate the same per-SKU line items as the shipment so the invoice
                    // shows the actual product(s) being refunded, not a generic "Refund" label.
                    const double absSubtotalSumR = qAbs(acc.subtotalSum);
                    const double refundVatRate = (absSubtotalSumR > 0.0001)
                        ? (qAbs(acc.taxSum) / absSubtotalSumR) : 0.0;
                    QList<LineItem> refundItems;
                    for (const auto &li : std::as_const(acc.lineItems))
                    {
                        if (li.subtotal == 0.0 || li.quantity <= 0)
                            continue;
                        const double taxedPerUnit = -qAbs(li.subtotal) * (1.0 + refundVatRate) / li.quantity;
                        const QString name = !li.name.isEmpty() ? li.name
                                           : (!li.sku.isEmpty() ? li.sku : QObject::tr("Refund"));
                        auto liRes = LineItem::create(li.sku, name, taxedPerUnit, refundVatRate, li.quantity);
                        if (liRes.ok())
                            refundItems.append(*liRes.value);
                    }
                    if (refundItems.isEmpty())
                    {
                        // Fallback: no per-SKU data — use a single generic line item
                        auto liRes = LineItem::create("", QObject::tr("Refund"), -acc.refundedAmount, refundVatRate, 1);
                        if (liRes.ok())
                            refundItems.append(*liRes.value);
                    }
                    if (!refundItems.isEmpty())
                    {
                        auto refundInfoRes = InvoicingInfo::create(
                            &ret.orderInfos->refunds.last(),
                            refundItems, std::nullopt, std::nullopt, std::nullopt);
                        if (refundInfoRes.ok())
                        {
                            ret.orderInfos->invoicingInfos.append(
                                InvoicingInfoWithId{ret.orderInfos->refunds.last().getId(),
                                                    *refundInfoRes.value});
                        }
                        else
                        {
                            qWarning() << "ImporterFileCommerceHQ: InvoicingInfo failed for refund"
                                       << orderNumber;
                        }
                    }
                }
            }
            else
            {
                // Partial refund — cannot attribute to a specific line item; store as clue
                ret.orderInfos->orderId_refundClues[orderNumber].append({acc.refundedAmount, currency, acc.refundDate.isValid() ? acc.refundDate : acc.firstDt.date()});
            }
        }
    }

    // Calculate date range from both shipments and refunds
    QDateTime minDt;
    QDateTime maxDt;
    auto updateRange = [&minDt, &maxDt](const QDateTime &candidate) {
        if (!minDt.isValid() || candidate < minDt)
            minDt = candidate;
        if (!maxDt.isValid() || candidate > maxDt)
            maxDt = candidate;
    };
    for (const auto &s : std::as_const(ret.orderInfos->shipments))
    {
        for (const auto &a : s.getActivities())
            updateRange(a.getDateTime());
    }
    for (const auto &r : std::as_const(ret.orderInfos->refunds))
    {
        for (const auto &a : r.getActivities())
            updateRange(a.getDateTime());
    }
    if (minDt.isValid())
    {
        ret.orderInfos->dateMin = minDt.date();
        ret.orderInfos->dateMax = maxDt.date();
    }

    co_return ret;
}
