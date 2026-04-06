#include "ImporterFileAmazonOrdersFBM.h"
#include "books/Activity.h"
#include "orders/InvoicingInfo.h"
#include "orders/LineItem.h"
#include "orders/Shipment.h"
#include "orders/Address.h"
#include "utils/CsvReader.h"
#include "utils/CsvHeader.h"
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QDebug>

DECLARE_IMPORTER_FILE(ImporterFileAmazonOrdersFBM)

QString ImporterFileAmazonOrdersFBM::getLabel() const
{
    return QObject::tr("Amazon FBM Orders Report");
}

ActivitySource ImporterFileAmazonOrdersFBM::getActivitySource() const
{
    ActivitySource s;
    s.type = ActivitySourceType::Report;
    s.channel = CHANNEL_AMAZON;
    s.reportOrMethode = QObject::tr("Amazon FBM Orders");
    return s;
}

QString ImporterFileAmazonOrdersFBM::getId() const
{
    return "AmazonOrdersFBM";
}

QMap<QString, AbstractImporter::ParamInfo> ImporterFileAmazonOrdersFBM::getRequiredParams() const
{
    return {};
}

QString ImporterFileAmazonOrdersFBM::getUniqueReportId(const QString &filePath) const
{
    return QFileInfo(filePath).fileName();
}

bool ImporterFileAmazonOrdersFBM::recomputeTaxes() const
{
    return true;
}

bool ImporterFileAmazonOrdersFBM::isWrongIfConflict() const
{
    return true;
}

bool ImporterFileAmazonOrdersFBM::fixRefundDate() const
{
    return false;
}

bool ImporterFileAmazonOrdersFBM::isGroupedOrders() const
{
    return true;
}

QString ImporterFileAmazonOrdersFBM::salesChannelToCountryCode(const QString &salesChannel)
{
    static const QHash<QString, QString> map = {
        {"amazon.com",    "US"},
        {"amazon.co.uk",  "GB"},
        {"amazon.de",     "DE"},
        {"amazon.fr",     "FR"},
        {"amazon.it",     "IT"},
        {"amazon.es",     "ES"},
        {"amazon.nl",     "NL"},
        {"amazon.pl",     "PL"},
        {"amazon.se",     "SE"},
        {"amazon.com.be", "BE"},
        {"amazon.ca",     "CA"},
        {"amazon.com.mx", "MX"},
        {"amazon.co.jp",  "JP"},
        {"amazon.com.au", "AU"},
        {"amazon.in",     "IN"},
        {"amazon.ae",     "AE"},
        {"amazon.com.br", "BR"},
        {"amazon.sg",     "SG"},
        {"amazon.com.sa", "SA"},
        {"amazon.com.tr", "TR"},
        {"amazon.eg",     "EG"},
    };
    return map.value(salesChannel.toLower().trimmed(), QString{});
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterFileAmazonOrdersFBM::_loadReport(
    const QString &filePath,
    std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing)
{
    Q_UNUSED(callbackAddIfMissing);

    AbstractImporter::ReturnOrderInfos ret;
    ret.orderInfos = QSharedPointer<AbstractImporter::OrderInfos>::create();

    // Amazon FBM order reports are tab-separated with no quoting
    CsvReader reader(filePath, "\t", "", true, "\n", 0, "UTF-8");
    if (!reader.readAll()) {
        ret.errorReturned = "Could not read file: " + filePath;
        co_return ret;
    }
    const auto *csvData = reader.dataRode();

    // Mandatory columns — pos() throws CsvHeaderException if a column is absent,
    // ensuring that a renamed column is caught immediately rather than silently
    // producing wrong data.
    int idxOrderId      = csvData->header.pos("order-id");
    int idxItemId       = csvData->header.pos("order-item-id");
    int idxDate         = csvData->header.pos("purchase-date");
    int idxCurrency     = csvData->header.pos("currency");
    int idxItemPrice    = csvData->header.pos("item-price");
    int idxItemTax      = csvData->header.pos("item-tax");
    int idxShipCountry  = csvData->header.pos("ship-country");
    int idxSalesChannel = csvData->header.pos("sales-channel");

    // Optional columns — absent columns return -1 and are handled gracefully
    auto optionalPos = [&](const QString &name) -> int {
        return csvData->header.contains(name) ? csvData->header.pos(name) : -1;
    };
    int idxShippingPrice   = optionalPos("shipping-price");
    int idxShippingTax     = optionalPos("shipping-tax");
    int idxSku             = optionalPos("sku");
    int idxProductName     = optionalPos("product-name");
    int idxQty             = optionalPos("quantity-purchased");
    int idxRecipientName   = optionalPos("recipient-name");
    int idxAddr1           = optionalPos("ship-address-1");
    int idxAddr2           = optionalPos("ship-address-2");
    int idxAddr3           = optionalPos("ship-address-3");
    int idxCity            = optionalPos("ship-city");
    int idxState           = optionalPos("ship-state");
    int idxPostcode        = optionalPos("ship-postal-code");
    int idxPhone           = optionalPos("ship-phone-number");
    int idxBuyerEmail      = optionalPos("buyer-email");
    int idxIsBusinessOrder = optionalPos("is-business-order");

    // Accumulate per order-id to merge multi-item orders into one Shipment
    QList<QString> orderIdOrder; // insertion order
    QHash<QString, QList<Activity>> orderIdActivities;
    QHash<QString, QList<LineItem>> orderIdLineItems;
    QSet<QString> addedAddresses;

    for (const auto &line : csvData->lines) {
        if (line.isEmpty()) {
            continue;
        }

        const QString orderId = line.value(idxOrderId).trimmed();
        const QString itemId  = line.value(idxItemId).trimmed();
        if (orderId.isEmpty() || itemId.isEmpty()) {
            continue;
        }

        // Parse ISO date with timezone offset (e.g. "2026-02-05T22:39:16-08:00")
        const QString &dateStr = line.value(idxDate);
        QDateTime dt = QDateTime::fromString(dateStr, Qt::ISODate);
        if (!dt.isValid()) {
            ret.errorReturned = QObject::tr("Invalid date format") + ": " + dateStr;
            co_return ret;
        }

        // Amounts
        double itemPrice     = line.value(idxItemPrice).toDouble();
        double itemTax       = line.value(idxItemTax).toDouble();
        double shippingPrice = (idxShippingPrice >= 0) ? line.value(idxShippingPrice).toDouble() : 0.0;
        double shippingTax   = (idxShippingTax   >= 0) ? line.value(idxShippingTax).toDouble()   : 0.0;

        // Destination country
        const QString destCountry = line.value(idxShipCountry).trimmed();

        // For US, CA and MX, Amazon is the marketplace facilitator: taxes are collected
        // by Amazon and must not appear in the seller's books.
        const bool isMktFacilitator = (destCountry == "US" || destCountry == "CA" || destCountry == "MX");
        if (isMktFacilitator) {
            itemTax     = 0.0;
            shippingTax = 0.0;
        }

        // Origin country — derived from the sales channel (e.g. "Amazon.com" → "US")
        const QString salesChannel  = line.value(idxSalesChannel).trimmed();
        const QString originCountry = salesChannelToCountryCode(salesChannel);

        // Business buyer flag
        bool isCompany = false;
        if (idxIsBusinessOrder >= 0) {
            isCompany = (line.value(idxIsBusinessOrder).trimmed().compare("true", Qt::CaseInsensitive) == 0);
        }

        // Activity amount bundles item revenue and shipping revenue so the full
        // seller income is captured in one posting line.
        const double totalTax   = itemTax + shippingTax;
        const double totalGross = itemPrice + totalTax + shippingPrice;
        const ::Amount amount(totalGross, totalTax);

        auto actResult = Activity::create(
            orderId,
            itemId,
            "",      // subActivityId
            dt,
            dt,      // dateTimeTax == dateTime
            line.value(idxCurrency).trimmed(),
            originCountry,
            destCountry,
            isCompany,
            QString{},           // countryCodeVatPaidTo — filled by TaxResolver on recomputeTaxes
            amount,
            TaxSource::Unknown,
            QString{},           // taxDeclaringCountryCode — filled by TaxResolver
            TaxScheme::Unknown,
            TaxJurisdictionLevel::Unknown,
            SaleType::Products
        );

        if (!actResult.ok()) {
            ret.errorReturned = "Activity Create Error for order: " + orderId;
            co_return ret;
        }

        if (!orderIdActivities.contains(orderId)) {
            orderIdOrder.append(orderId);
            orderIdActivities[orderId] = QList<Activity>();
            orderIdLineItems[orderId]  = QList<LineItem>();
        }
        orderIdActivities[orderId].append(*actResult.value);

        // Build a line item for InvoicingInfo
        {
            const QString sku  = (idxSku         >= 0) ? line.value(idxSku)         : QString{};
            QString title      = (idxProductName  >= 0) ? line.value(idxProductName) : QString{};
            if (title.isEmpty()) {
                title = sku;
            }
            const int qty = (idxQty >= 0) ? qMax(1, line.value(idxQty).toInt()) : 1;
            if (!title.isEmpty() && qAbs(totalGross) > 0.001) {
                const double netRevenue = itemPrice + shippingPrice;
                const double vatRate = (qAbs(netRevenue) > 0.001) ? (totalTax / netRevenue) : 0.0;
                auto liRes = LineItem::create(sku, title, totalGross / qty, vatRate, qty);
                if (liRes.ok()) {
                    orderIdLineItems[orderId].append(*liRes.value);
                }
            }
        }

        // Address — stored once per order-id
        if (!addedAddresses.contains(orderId)) {
            Address addr(
                (idxRecipientName >= 0) ? line.value(idxRecipientName) : QString{},
                (idxAddr1         >= 0) ? line.value(idxAddr1)         : QString{},
                (idxAddr2         >= 0) ? line.value(idxAddr2)         : QString{},
                (idxAddr3         >= 0) ? line.value(idxAddr3)         : QString{},
                (idxCity          >= 0) ? line.value(idxCity)          : QString{},
                (idxPostcode      >= 0) ? line.value(idxPostcode)      : QString{},
                destCountry,
                (idxState         >= 0) ? line.value(idxState)         : QString{},
                (idxBuyerEmail    >= 0) ? line.value(idxBuyerEmail)    : QString{},
                (idxPhone         >= 0) ? line.value(idxPhone)         : QString{},
                "",  // companyName
                ""   // taxId
            );
            ret.orderInfos->orderAddresses.append({orderId, addr});
            addedAddresses.insert(orderId);
        }

        ret.orderInfos->orderId_infos.insert(
            orderId, OrderManager::OrderInfo{salesChannel, isGroupedOrders(), ""});
    }

    // Build one Shipment per order-id (preserving insertion order)
    for (const QString &oid : std::as_const(orderIdOrder)) {
        Shipment shipment(orderIdActivities[oid], "", isGroupedOrders());
        ret.orderInfos->shipments.append(shipment);

        const QList<LineItem> &lineItems = orderIdLineItems.value(oid);
        if (!lineItems.isEmpty()) {
            auto infoRes = InvoicingInfo::create(
                &ret.orderInfos->shipments.last(), lineItems,
                std::nullopt, std::nullopt, std::nullopt);
            if (infoRes.ok()) {
                ret.orderInfos->invoicingInfos.append({oid, *infoRes.value});
            }
        }
    }

    // Compute date range over all shipments
    if (!ret.orderInfos->shipments.isEmpty()) {
        QDateTime minDt = ret.orderInfos->shipments.first().getActivities().first().getDateTime();
        QDateTime maxDt = minDt;
        for (const auto &s : std::as_const(ret.orderInfos->shipments)) {
            for (const auto &a : s.getActivities()) {
                if (a.getDateTime() < minDt) {
                    minDt = a.getDateTime();
                }
                if (a.getDateTime() > maxDt) {
                    maxDt = a.getDateTime();
                }
            }
        }
        ret.orderInfos->dateMin = minDt.date();
        ret.orderInfos->dateMax = maxDt.date();
    }

    co_return ret;
}
