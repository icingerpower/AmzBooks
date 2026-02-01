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
    
    const QStringList required = {
        "Amazon Order Id", "Shipment ID", "Shipment Date", "Currency", 
        "Item Price", "Item Tax", "FC", "Delivery Country Code"
    };
    
    for (const QString &col : required) {
        if (csvData->header.pos(col) == -1) {
             qWarning() << "Missing column:" << col;
             throw CsvHeaderException();
        }
    }

    // Initialize FbaCentersTable
    FbaCentersTable fbaTable(m_workingDirectory);

    int idxOrderId = csvData->header.pos("Amazon Order Id");
    int idxShipId = csvData->header.pos("Shipment ID");
    int idxShipItemId = csvData->header.pos("Shipment Item ID");
    int idxDate = csvData->header.pos("Shipment Date"); 
    int idxCurrency = csvData->header.pos("Currency");
    int idxItemPrice = csvData->header.pos("Item Price");
    int idxItemTax = csvData->header.pos("Item Tax");
    int idxFC = csvData->header.pos("FC");
    int idxDelivCountry = csvData->header.pos("Delivery Country Code");

    int idxName = csvData->header.pos("Recipient Name");
    int idxAddr1 = csvData->header.pos("Delivery Address 1");
    int idxAddr2 = csvData->header.pos("Delivery Address 2");
    int idxAddr3 = csvData->header.pos("Delivery Address 3");
    int idxCity = csvData->header.pos("Delivery City/Town");
    int idxCounty = csvData->header.pos("Delivery County");
    int idxPostcode = csvData->header.pos("Delivery Postcode");
    int idxPhone = csvData->header.pos("Delivery Phone Number");
    int idxEmail = csvData->header.pos("Buyer E-mail");
    
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
        try {
            // Forward the callback to allow user to add missing FBA centers
            originCountry = co_await fbaTable.getCountryCode(fc, callbackAddIfMissing);
        } catch (const std::exception &e) {
            ret.errorReturned = QString("FC Error: ") + e.what();
            co_return ret;
        }

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
