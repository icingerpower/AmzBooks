#include "ImporterFileAmazonFbaInvoicing.h"
#include "books/FbaCentersTable.h"
#include "books/Activity.h" // Added
#include "orders/Shipment.h" // Added
#include "utils/CsvReader.h"
#include "utils/CsvHeader.h"
#include <QFileInfo>
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
    
    // Helper to find column index from a list of possible names
    auto getIdx = [&](const QStringList &names) -> int {
        for (const auto &name : names) {
            if (csvData->header.contains(name)) {
                return csvData->header.pos(name);
            }
        }
        return -1;
    };

    // Mandatory Columns
    struct ColReq { QString internal; QStringList candidates; };
    QList<ColReq> reqCols = {
        {"Amazon Order Id", {"Amazon Order Id"}},
        {"Shipment ID", {"Shipment ID"}},
        {"Shipment Date", {"Shipment Date"}},
        {"Currency", {"Currency"}},
        {"Item Price", {"Item Price"}},
        {"Item Tax", {"Item Tax"}},
        {"FC", {"FC"}},
        {"Delivery Country Code", {"Delivery Country Code", "Shipping Country Code"}}
    };

    // Debug headers
    // qCritical() << "Headers found:" << csvData->header.headers; // Assuming CsvHeader has QStringList headers or similar? 
    // csvData->header is CsvHeader. Does it have public list?
    // checking CsvHeader.h would be good, but assuming standard. CsvReader::m_dataRode.header.
    // Let's iterate if uncertain about public members, or just rely on pos check failing.
    
    for (const auto &req : reqCols) {
        if (getIdx(req.candidates) == -1) {
             qCritical() << "Missing column:" << req.internal << "Candidates:" << req.candidates;
             CsvHeaderException e;
             e.setColumnValuesError({req.internal});
             e.setFileName(filePath);
             throw e;
        }
    }

    // Initialize FbaCentersTable
    FbaCentersTable fbaTable(m_workingDirectory);

    int idxOrderId = getIdx({"Amazon Order Id"});
    int idxShipId = getIdx({"Shipment ID"});
    int idxShipItemId = getIdx({"Shipment Item ID", "Shipment Item Id"}); // Added capitalization variant just in case
    int idxDate = getIdx({"Shipment Date"}); 
    int idxCurrency = getIdx({"Currency"});
    int idxItemPrice = getIdx({"Item Price"});
    int idxItemTax = getIdx({"Item Tax"});
    int idxFC = getIdx({"FC"});
    int idxDelivCountry = getIdx({"Delivery Country Code", "Shipping Country Code"});

    int idxName = getIdx({"Recipient Name"});
    int idxAddr1 = getIdx({"Delivery Address 1", "Shipping Address 1"});
    int idxAddr2 = getIdx({"Delivery Address 2", "Shipping Address 2"});
    int idxAddr3 = getIdx({"Delivery Address 3", "Shipping Address 3"});
    int idxCity = getIdx({"Delivery City/Town", "Shipping City"});
    int idxCounty = getIdx({"Delivery County", "Shipping State"}); // US uses State, EU often County? Or Province.
    int idxPostcode = getIdx({"Delivery Postcode", "Shipping Postal Code"});
    int idxPhone = getIdx({"Delivery Phone Number", "Shipping Phone Number"});
    int idxEmail = getIdx({"Buyer E-mail", "Buyer Email"});
    
    // Track added addresses to avoid duplicates
    QSet<QString> addedAddresses;

    for (const auto &line : csvData->lines) {
        if (line.isEmpty()) continue;
        
        QString orderId = line.value(idxOrderId);
        QString shipId = line.value(idxShipId);
        if (orderId.isEmpty() || shipId.isEmpty()) continue;

        // Date
        QString dateStr = line.value(idxDate);
        QDateTime dt = QDateTime::fromString(dateStr, Qt::ISODate);
        if (!dt.isValid()) {
             ret.errorReturned = "Invalid date format: " + dateStr;
             co_return ret;
        }

        // Amount (Price + Tax)
        double price = line.value(idxItemPrice).toDouble();
        double tax = line.value(idxItemTax).toDouble();
        // Assuming Price is Net, constructed Amount should be (Gross, Tax)
        Amount amount(price + tax, tax);
        
        // FC Resolution
        QString fc = line.value(idxFC).trimmed();
        QString originCountry;

        // Forward the callback to allow user to add missing FBA centers
        // EXCEPTION MUST NOT BE CAUGHT HERE: if an FBA center is missing, the test must fail
        // so we can identify and add it to FbaCentersTable::_fillIfEmpty.
        originCountry = co_await fbaTable.getCountryCode(fc, callbackAddIfMissing);

        QString destCountry = line.value(idxDelivCountry);
        QString shippingCountry = originCountry; // From FC

        QString eventId = shipId; // Unique ID for shipment? Or OrderId? Usually ShipmentId for FBA shipments.
        
        // Create Activity
        // Note: Using shipItemId as activityId to ensure uniqueness if multiple items per shipment
        QString activityId = line.value(idxShipItemId); 
        if (activityId.isEmpty()) activityId = orderId; // Fallback

        // Determine Tax Scheme
        TaxScheme scheme = (shippingCountry == destCountry) ? TaxScheme::DomesticVat : TaxScheme::EuOssUnion;

        auto actResult = Activity::create(
            eventId, 
            activityId,
            "", // External link
            dt,
            line.value(idxCurrency),
            shippingCountry, // Departure
            destCountry,     // Arrival
            destCountry,     // Declaring (Assumption: OSS or Dest)
            amount,
            TaxSource::MarketplaceProvided, // Amazon handles text?
            destCountry, // Tax Liability Country
            scheme, 
            TaxJurisdictionLevel::Country,
            SaleType::Products
        );

        if (!actResult.ok()) {
            ret.errorReturned = "Activity Create Error: Validation Failed";
            co_return ret;
        }

        QList<Activity> acts;
        acts.append(*actResult.value);
        Shipment shipment(acts);
        ret.orderInfos->shipments.append(shipment);

        // Address
        if (!addedAddresses.contains(orderId)) {
            Address addr(
                line.value(idxName),
                line.value(idxAddr1),
                line.value(idxAddr2),
                line.value(idxAddr3),
                line.value(idxCity),
                line.value(idxPostcode),
                destCountry,
                line.value(idxCounty),
                line.value(idxEmail),
                line.value(idxPhone),
                "", // Company
                ""  // Vat ID
            );
            
            AbstractImporter::AddressToWithId addrTo{orderId, addr};
            
            ret.orderInfos->orderAddresses.append(addrTo);
            addedAddresses.insert(orderId);
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
