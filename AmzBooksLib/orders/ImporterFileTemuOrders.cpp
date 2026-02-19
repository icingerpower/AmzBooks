#include "ImporterFileTemuOrders.h"
#include "books/Activity.h"
#include "orders/Shipment.h"
#include "utils/CsvReader.h"
#include "utils/CsvHeader.h"
#include <QFileInfo>
#include <QDebug>
#include <QRegularExpression>
#include <QLocale>

namespace {
    QString countryNameToCode(const QString &countryName)
    {
        static const QMap<QString, QString> countryMap = {
            {"france", "FR"},
            {"germany", "DE"},
            {"italy", "IT"},
            {"spain", "ES"},
            {"portugal", "PT"},
            {"belgium", "BE"},
            {"netherlands", "NL"},
            {"austria", "AT"},
            {"poland", "PL"},
            {"czech republic", "CZ"},
            {"czechia", "CZ"},
            {"sweden", "SE"},
            {"denmark", "DK"},
            {"finland", "FI"},
            {"ireland", "IE"},
            {"greece", "GR"},
            {"hungary", "HU"},
            {"romania", "RO"},
            {"bulgaria", "BG"},
            {"slovakia", "SK"},
            {"croatia", "HR"},
            {"slovenia", "SI"},
            {"luxembourg", "LU"},
            {"estonia", "EE"},
            {"latvia", "LV"},
            {"lithuania", "LT"},
            {"malta", "MT"},
            {"cyprus", "CY"},
            {"united kingdom", "GB"},
            {"uk", "GB"},
            {"switzerland", "CH"},
            {"norway", "NO"},
            {"united states", "US"},
            {"usa", "US"}
        };
        
        QString lowerName = countryName.trimmed().toLower();
        
        // Check direct match
        if (countryMap.contains(lowerName))
        {
            return countryMap[lowerName];
        }
        
        // If already a 2-letter code, return as is
        if (countryName.trimmed().length() == 2)
        {
            return countryName.trimmed().toUpper();
        }
        
        // Fallback: return original trimmed (might be a code already)
        qWarning() << "Unknown country name:" << countryName;
        return countryName.trimmed();
    }
}

DECLARE_IMPORTER_FILE(ImporterFileTemuOrders)

QString ImporterFileTemuOrders::getLabel() const
{
    return QObject::tr("Temu Orders Report");
}

ActivitySource ImporterFileTemuOrders::getActivitySource() const
{
    ActivitySource s;
    s.type = ActivitySourceType::Report;
    s.channel = CHANNEL_TEMU;
    s.reportOrMethode = QObject::tr("Temu Orders Export");
    return s;
}

QString ImporterFileTemuOrders::getId() const
{
    return "TemuOrders";
}

QMap<QString, AbstractImporter::ParamInfo> ImporterFileTemuOrders::getRequiredParams() const
{
    return {};
}

QString ImporterFileTemuOrders::getUniqueReportId(const QString &filePath) const
{
    return QFileInfo(filePath).fileName();
}

bool ImporterFileTemuOrders::recomputeTaxes() const
{
    return true;
}

bool ImporterFileTemuOrders::isWrongIfConflict() const
{
    return true;
}

double ImporterFileTemuOrders::parseEuropeanPrice(const QString &priceStr)
{
    // Handle European price format: "9,99€" or "0,00€"
    QString cleaned = priceStr.trimmed();
    
    // Remove currency symbol (€) and any whitespace
    cleaned.remove(QChar(0x20AC)); // Euro sign
    cleaned.remove("EUR");
    cleaned.remove("€");
    cleaned = cleaned.trimmed();
    
    // Handle empty string
    if (cleaned.isEmpty())
    {
        return 0.0;
    }
    
    // Replace comma with dot for decimal parsing
    cleaned.replace(',', '.');
    
    // Remove any thousand separators (spaces or dots that are not the last one)
    // In European format, thousand separator is often a space or a dot
    // Since we already replaced comma with dot, we need to handle dots as thousand separators
    // Check if there are multiple dots
    int dotCount = cleaned.count('.');
    if (dotCount > 1)
    {
        // Keep only the last dot as decimal separator
        int lastDotPos = cleaned.lastIndexOf('.');
        for (int i = 0; i < lastDotPos; ++i)
        {
            if (cleaned[i] == '.')
            {
                cleaned[i] = QChar(); // Remove
            }
        }
        cleaned.remove(QChar());
    }
    
    // Remove spaces (thousand separator)
    cleaned.remove(' ');
    
    bool ok;
    double value = cleaned.toDouble(&ok);
    if (!ok)
    {
        qWarning() << "Failed to parse price:" << priceStr << "cleaned:" << cleaned;
        return 0.0;
    }
    return value;
}

QDateTime ImporterFileTemuOrders::parseTemuDate(const QString &dateStr)
{
    // Format: "Jan 27, 2026, 2:20 pm CET(UTC+1)"
    // We need to extract the date/time part before the timezone info
    
    QString cleaned = dateStr.trimmed();
    
    // Remove timezone info: everything from the first uppercase letter sequence at the end
    // Pattern: "CET(UTC+1)" or similar
    static QRegularExpression tzRegex(R"(\s+[A-Z]{2,}.*$)");
    cleaned.remove(tzRegex);
    cleaned = cleaned.trimmed();
    
    // Try parsing with various formats
    QStringList formats = {
        "MMM d, yyyy, h:mm ap",
        "MMM dd, yyyy, h:mm ap", 
        "MMM d, yyyy, hh:mm ap",
        "MMM dd, yyyy, hh:mm ap",
        "MMMM d, yyyy, h:mm ap",
        "MMMM dd, yyyy, h:mm ap"
    };
    
    QLocale enLocale(QLocale::English, QLocale::UnitedStates);
    
    for (const QString &format : formats)
    {
        QDateTime dt = enLocale.toDateTime(cleaned, format);
        if (dt.isValid())
        {
            return dt;
        }
    }
    
    // Fallback: try ISO format
    QDateTime dt = QDateTime::fromString(cleaned, Qt::ISODate);
    if (dt.isValid())
    {
        return dt;
    }
    
    qWarning() << "Failed to parse Temu date:" << dateStr << "cleaned:" << cleaned;
    return QDateTime();
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterFileTemuOrders::_loadReport(
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


    // Get column indices
    int idxOrderId = csvData->header.pos("Order ID");
    int idxOrderItemId = csvData->header.pos("Order item ID");
    int idxStatus = csvData->header.pos("order status");
    int idxDate = csvData->header.pos("purchase date");
    int idxCountry = csvData->header.pos("ship country");
    int idxBasePrice = csvData->header.pos("Base price total  after discount");
    int idxProductTax = csvData->header.pos("Product Tax");
    
    // Optional columns - use contains() to avoid exception
    int idxSku = csvData->header.contains("contribution sku") ? csvData->header.pos("contribution sku") : -1;
    int idxQuantity = csvData->header.contains("quantity purchased") ? csvData->header.pos("quantity purchased") : -1;
    int idxRefund = csvData->header.contains("Product refund") ? csvData->header.pos("Product refund") : -1;
    int idxRetailPrice = csvData->header.contains("Retail price total after discounts(tax excl.)") 
        ? csvData->header.pos("Retail price total after discounts(tax excl.)") : -1;
    
    for (const auto &line : csvData->lines)
    {
        if (line.isEmpty())
        {
            continue;
        }
        
        QString orderId = line.value(idxOrderId).trimmed();
        QString orderItemId = line.value(idxOrderItemId).trimmed();
        QString status = line.value(idxStatus).trimmed();
        
        if (orderId.isEmpty() || orderItemId.isEmpty())
        {
            continue;
        }
        
        // Skip canceled orders
        if (status.compare("Canceled", Qt::CaseInsensitive) == 0)
        {
            continue;
        }

        // Parse date
        QString dateStr = line.value(idxDate);
        QDateTime dt = parseTemuDate(dateStr);
        if (!dt.isValid())
        {
            ret.errorReturned = "Invalid date format: " + dateStr;
            co_return ret;
        }

        // Parse amounts
        double basePrice = parseEuropeanPrice(line.value(idxBasePrice));
        double productTax = parseEuropeanPrice(line.value(idxProductTax));
        
        // Use retail price (tax excluded) if available, otherwise base price
        double netAmount = basePrice;
        if (idxRetailPrice != -1 && !line.value(idxRetailPrice).trimmed().isEmpty())
        {
            double retailPrice = parseEuropeanPrice(line.value(idxRetailPrice));
            if (retailPrice > 0)
            {
                netAmount = retailPrice;
            }
        }
        
        // Check for refunds
        double refundAmount = 0.0;
        if (idxRefund != -1)
        {
            refundAmount = parseEuropeanPrice(line.value(idxRefund));
        }
        
        // If this is a full refund, skip (or handle as refund)
        if (refundAmount > 0 && qAbs(refundAmount - (netAmount + productTax)) < 0.01)
        {
            // Full refund - skip for now, refunds handled separately
            continue;
        }

        // Destination country - convert name to ISO code
        QString destCountry = countryNameToCode(line.value(idxCountry));
        
        // For Temu EU, assume shipping from FR (France based on data)
        QString originCountry = "FR";
        
        // Currency is EUR (based on € symbol in prices)
        QString currency = "EUR";
        
        // Create Amount (gross includes tax)
        ::Amount amount(netAmount + productTax, productTax);
        
        // Determine Tax Scheme
        TaxScheme scheme = (originCountry == destCountry) 
            ? TaxScheme::DomesticVat 
            : TaxScheme::EuOssUnion;

        // Create Activity
        auto actResult = Activity::create(
            orderId,              // eventId
            orderItemId,          // activityId
            "",                   // subActivityId
            dt,                   // dateTime
            dt,                   // dateTimeTax
            currency,             // currency
            originCountry,        // countryCodeFrom
            destCountry,          // countryCodeTo
            false,                // isCompany (Temu is B2C marketplace)
            destCountry,          // countryCodeVatPaidTo
            amount,               // amount
            TaxSource::MarketplaceProvided,
            destCountry,          // taxDeclaringCountryCode
            scheme,
            TaxJurisdictionLevel::Country,
            SaleType::Products
        );

        if (!actResult.ok())
        {
            ret.errorReturned = "Activity Create Error for order: " + orderId;
            co_return ret;
        }

        QList<Activity> acts;
        acts.append(*actResult.value);
        Shipment shipment(acts);
        ret.orderInfos->shipments.append(shipment);
    }

    // Calculate min/max dates
    if (!ret.orderInfos->shipments.isEmpty())
    {
        QDateTime min = ret.orderInfos->shipments.first().getActivities().first().getDateTime();
        QDateTime max = min;
        for (const auto &s : ret.orderInfos->shipments)
        {
            for (const auto &a : s.getActivities())
            {
                if (a.getDateTime() < min)
                {
                    min = a.getDateTime();
                }
                if (a.getDateTime() > max)
                {
                    max = a.getDateTime();
                }
            }
        }
        ret.orderInfos->dateMin = min.date();
        ret.orderInfos->dateMax = max.date();
    }

    co_return ret;
}
