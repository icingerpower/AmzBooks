#include "ImporterFileAmazonFbaInvoicing.h"
#include "books/FbaCentersTable.h"
#include "books/Activity.h"
#include "CountriesEu.h"
#include "orders/InvoicingInfo.h"
#include "orders/LineItem.h"
#include "orders/Shipment.h"
#include "utils/CsvReader.h"
#include "utils/CsvHeader.h"
#include <QFileInfo>
#include <QHash>
#include <QDebug>
#include <exception>

DECLARE_IMPORTER_FILE(ImporterFileAmazonFbaInvoicing)

QString ImporterFileAmazonFbaInvoicing::getLabel() const
{
    return QObject::tr("Amazon FBA Invoicing Report");
}

ActivitySource ImporterFileAmazonFbaInvoicing::getActivitySource() const
{
    ActivitySource s;
    s.type = ActivitySourceType::Report;
    s.channel = CHANNEL_AMAZON;
    s.reportOrMethode = QObject::tr("Amazon FBA Invoicing");
    return s;
}

QString ImporterFileAmazonFbaInvoicing::getId() const
{
    return "AmazonFbaInvoicing";
}

QMap<QString, AbstractImporter::ParamInfo> ImporterFileAmazonFbaInvoicing::getRequiredParams() const
{
    return {};
}

QString ImporterFileAmazonFbaInvoicing::getUniqueReportId(const QString &filePath) const
{
    return QFileInfo(filePath).fileName();
}

bool ImporterFileAmazonFbaInvoicing::recomputeTaxes() const
{
    return true;
}

bool ImporterFileAmazonFbaInvoicing::isWrongIfConflict() const
{
    return true;
}

bool ImporterFileAmazonFbaInvoicing::fixRefundDate() const
{
    return false;
}

bool ImporterFileAmazonFbaInvoicing::isGroupedOrders() const
{
    return true;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterFileAmazonFbaInvoicing::_loadReport(
    const QString &filePath,
    std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing)
{
    AbstractImporter::ReturnOrderInfos ret;
    ret.orderInfos = QSharedPointer<AbstractImporter::OrderInfos>::create();

    // Use strict mode for CsvReader if possible, or manual check
    CsvReader reader(filePath, ",", "\"", true, "\n", 0, "UTF-8");
    if (!reader.readAll()) { // readAll bool return? 
        ret.errorReturned = "Could not read CSV file: " + filePath;
        co_return ret;
    }
    const auto *csvData = reader.dataRode();

    // Check Mandatory Headers
    // "Amazon Order Id","Shipment ID","Shipment Date","Currency","Item Price","Item Tax","FC","Delivery Country Code"
    // "Recipient Name","Delivery Address 1","Delivery City/Town","Delivery Postcode"
    
    // Note: Prompt said "check CsvHeaderException is raised"
    // So we should verify headers and throw this exception if missing.
    // Assuming CsvHeaderException is defined in utils/CsvHeader.h
    
    // Use the shared config directory if set (so the pane and importer use the same fbacenters.csv).
    const QDir fbaDir = m_sharedConfigDirectoryPath.isEmpty()
        ? m_workingDirectory
        : QDir(m_sharedConfigDirectoryPath);
    FbaCentersTable fbaTable(fbaDir);

    // Helper for optional columns (returns -1 if missing)
    auto getOptionalPos = [&](const QStringList &names) -> int {
        for (const auto &name : names) {
             if (csvData->header.contains(name)) {
                 return csvData->header.pos(name);
             }
        }
        return -1;
    };

    // Mandatory columns - use header.pos() which throws CsvHeaderException if missing
    int idxOrderId = csvData->header.pos("Amazon Order Id");
    int idxShipId = csvData->header.pos("Shipment ID");
    int idxDate = csvData->header.pos("Shipment Date"); 
    int idxCurrency = csvData->header.pos("Currency");
    int idxItemPrice = csvData->header.pos("Item Price");
    int idxItemTax = csvData->header.pos("Item Tax");
    int idxFC = csvData->header.pos("FC");
    int idxDelivCountry = csvData->header.pos(QStringList{"Delivery Country Code", "Shipping Country Code"});
    int idxSalesChannel = csvData->header.pos("Sales Channel");

    // Optional columns
    //int idxShipItemId = getOptionalPos(QStringList{"Shipment Item ID", "Shipment Item Id"});
    int idxName = getOptionalPos(QStringList{"Recipient Name"});
    int idxAddr1 = getOptionalPos(QStringList{"Delivery Address 1", "Shipping Address 1"});
    int idxAddr2 = getOptionalPos(QStringList{"Delivery Address 2", "Shipping Address 2"});
    int idxAddr3 = getOptionalPos(QStringList{"Delivery Address 3", "Shipping Address 3"});
    int idxCity = getOptionalPos(QStringList{"Delivery City/Town", "Shipping City"});
    int idxCounty = getOptionalPos(QStringList{"Delivery County", "Shipping State/Province/Region"});
    int idxPostcode = getOptionalPos(QStringList{"Delivery Postcode", "Shipping Postal Code"});
    int idxPhone = getOptionalPos(QStringList{"Delivery Phone Number", "Shipping Phone Number"});
    int idxEmail = getOptionalPos(QStringList{"Buyer E-mail", "Buyer Email", "Buyer Name"});
    int idxTitle = getOptionalPos(QStringList{"Title"});
    int idxSku = getOptionalPos(QStringList{"Merchant SKU"});
    int idxQty = getOptionalPos(QStringList{"Dispatched Quantity"});

    // Track added addresses to avoid duplicates
    QSet<QString> addedAddresses;

    // Accumulate activities and line items per shipId to merge multi-item shipments into one Shipment
    QList<QString> shipIdOrder;
    QHash<QString, QList<Activity>> shipIdActivities;
    QHash<QString, QList<LineItem>> shipIdLineItems;

    for (const auto &line : csvData->lines) {
        if (line.isEmpty()) {
            continue;
        }
        
        const QString &orderId = line.value(idxOrderId);
        if (orderId.isEmpty()) {
            continue;
        }
        const QString &shipId = line.value(idxShipId);
        // When Shipment ID is empty (can happen in Amazon US reports), fall back to Order ID
        // as the grouping key so the row is still processed rather than silently dropped.
        const QString &groupKey = shipId.isEmpty() ? orderId : shipId;
        const QString &name = line.value(idxName);
        const QString &email = line.value(idxEmail);
        if (name.isEmpty() && email.isEmpty()) { // Vine refunded order
            continue;
        }

        // Date
        const QString &dateStr = line.value(idxDate);
        QDateTime dt = QDateTime::fromString(dateStr, Qt::ISODate);
        if (!dt.isValid()) {
             ret.errorReturned = QObject::tr("Invalid date format") + ": " + dateStr;
             co_return ret;
        }

        // Amount (Price + Tax)
        double price = line.value(idxItemPrice).toDouble();
        double tax = line.value(idxItemTax).toDouble();

        // FC Resolution
        const QString &fc = line.value(idxFC).trimmed();
        QString originCountry;

        // Forward the callback to allow user to add missing FBA centers
        // EXCEPTION MUST NOT BE CAUGHT HERE: if an FBA center is missing, the test must fail
        // so we can identify and add it to FbaCentersTable::_fillIfMissing.
        originCountry = co_await fbaTable.getCountryCode(fc, callbackAddIfMissing, line.value(idxSalesChannel));

        const QString &destCountry = line.value(idxDelivCountry);
        const QString &shippingCountry = originCountry; // From FC

        // For US (amazon.com), MX, and CA marketplaces, Amazon acts as marketplace
        // facilitator for taxes. Any tax column (Item Tax, Shipping Tax, Gift Wrap Tax,
        // etc.) must not be included in the seller's bookkeeping.
        if (destCountry == "US" || destCountry == "MX" || destCountry == "CA") {
            tax = 0.0;
        }

        // Amount is constructed after the country check so that the zeroed tax is used.
        ::Amount amount(price + tax, tax);

        // Guess isCompany: GB->GB with 0 tax suggests domestic UK B2B;
        // EU->EU (different countries) with 0 tax suggests intra-Community B2B supply.
        bool isCompany = false;
        if (qFuzzyIsNull(tax)) {
            bool fromGb = (originCountry == "GB");
            bool toGb = (destCountry == "GB");
            bool fromEu = CountriesEu::isEuMember(originCountry, dt.date());
            bool toEu = CountriesEu::isEuMember(destCountry, dt.date());
            if (qAbs(tax) < 0.001 && qAbs(price) > 0.001) {
                if (fromGb && toGb) {
                    isCompany = true;
                } else if (fromEu && toEu && originCountry != destCountry) {
                    isCompany = true;
                }
            }
        }

        const QString &eventId = orderId; // Unique ID for shipment? Or OrderId? Usually ShipmentId for FBA shipments.

        // Create Activity
        // Note: Using groupKey (shipId, or orderId when shipId is empty) as activityId
        const QString &activityId = groupKey;

        auto actResult = Activity::create(
                    eventId,
                    activityId,
                    "", // External link
                    dt,
                    dt, // dateTimeTax same as dateTime
                    line.value(idxCurrency),
                    shippingCountry, // Departure
                    destCountry,     // Arrival
                    isCompany,
                    QString{},     // Declaring (Assumption: OSS or Dest)
                    amount,
                    TaxSource::Unknown,
                    QString{}, // Tax Liability Country
                    TaxScheme::Unknown,
                    TaxJurisdictionLevel::Unknown,
                    SaleType::Products
                    );

        if (!actResult.ok()) {
            ret.errorReturned = "Activity Create Error: Validation Failed";
            co_return ret;
        }

        if (!shipIdActivities.contains(groupKey)) {
            shipIdOrder.append(groupKey);
            shipIdActivities[groupKey] = QList<Activity>();
            shipIdLineItems[groupKey] = QList<LineItem>();
        }
        shipIdActivities[groupKey].append(*actResult.value);

        // Line item for InvoicingInfo (one per CSV row / item)
        {
            const QString sku = (idxSku >= 0) ? line.value(idxSku) : QString();
            QString title = (idxTitle >= 0) ? line.value(idxTitle) : QString();
            if (title.isEmpty()) {
                title = sku;
            }
            const int qty = (idxQty >= 0) ? qMax(1, line.value(idxQty).toInt()) : 1;
            const double taxedTotal = price + tax;
            const double vatRate = (qAbs(price) > 0.001) ? (tax / price) : 0.0;
            if (!title.isEmpty() && qAbs(taxedTotal) > 0.001) {
                auto liRes = LineItem::create(sku, title, taxedTotal / qty, vatRate, qty);
                if (liRes.ok()) {
                    shipIdLineItems[groupKey].append(*liRes.value);
                }
            }
        }

        // Address
        if (!addedAddresses.contains(orderId)) {
            Address addr(
                name,
                line.value(idxAddr1),
                line.value(idxAddr2),
                line.value(idxAddr3),
                line.value(idxCity),
                line.value(idxPostcode),
                destCountry,
                line.value(idxCounty),
                email,
                line.value(idxPhone),
                "", // Company
                ""  // Vat ID
            );
            
            AbstractImporter::AddressToWithId addrTo{orderId, addr};
            
            ret.orderInfos->orderAddresses.append(addrTo);
            addedAddresses.insert(orderId);
        }
        
        const QString &salesChannel = line.value(idxSalesChannel);
        ret.orderInfos->orderId_infos.insert(orderId, OrderManager::OrderInfo{salesChannel, isGroupedOrders(), ""});
    }

    // Build one Shipment per unique shipId (preserving insertion order)
    for (const QString &sId : shipIdOrder) {
        Shipment shipment(shipIdActivities[sId], "", isGroupedOrders());
        ret.orderInfos->shipments.append(shipment);

        // Create InvoicingInfo from the accumulated line items so that invoice
        // generation can find invoicing data even for orders that only appear in
        // the FBA invoicing report (e.g. non-EU exports not in the VAT EU report).
        const QList<LineItem> &lineItems = shipIdLineItems.value(sId);
        if (!lineItems.isEmpty()) {
            auto infoRes = InvoicingInfo::create(
                &ret.orderInfos->shipments.last(), lineItems,
                std::nullopt, std::nullopt, std::nullopt);
            if (infoRes.ok()) {
                ret.orderInfos->invoicingInfos.append({sId, *infoRes.value});
            }
        }
    }

    // Min/Max dates
    // ... logic if needed, usually managed by caller or importer helper?
    // AbstractImporter doesn't auto-set dates in OrderInfos. We should set them.
    if (!ret.orderInfos->shipments.isEmpty()) {
        QDateTime min = ret.orderInfos->shipments.first().getActivities().first().getDateTime();
        QDateTime max = min;
        for (const auto &s : ret.orderInfos->shipments) {
            for (const auto &a : s.getActivities()) {
                if (a.getDateTime() < min) min = a.getDateTime();
                if (a.getDateTime() > max) max = a.getDateTime();
            }
        }
        ret.orderInfos->dateMin = min.date();
        ret.orderInfos->dateMax = max.date();
    }

    co_return ret;
}
