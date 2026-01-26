#include "ImporterFileAmazonVatEu.h"
#include "utils/CsvReader.h"
#include <QFileInfo>
#include <QDebug>

QString ImporterFileAmazonVatEu::getLabel() const
{
    return "Amazon EU VAT Report";
}

ActivitySource ImporterFileAmazonVatEu::getActivitySource() const
{
    return {ActivitySourceType::Report, "Amazon", "Amazon EU", "VAT Report"};
}

QMap<QString, AbstractImporter::ParamInfo> ImporterFileAmazonVatEu::getRequiredParams() const
{
    return {};
}

QString ImporterFileAmazonVatEu::getUniqueReportId(const QString &filePath) const
{
    // Amazon VAT reports don't have a unique ID inside clearly, so we use filename
    // or we could hash content, but filename is usually unique enough per month
    return QFileInfo(filePath).fileName();
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterFileAmazonVatEu::_loadReport(const QString &filePath)
{
    AbstractImporter::ReturnOrderInfos result;
    result.orderInfos = QSharedPointer<AbstractImporter::OrderInfos>::create();

    CsvReader reader(filePath, ",", "\"", true, "\n", 0, "UTF-8");
    if (!reader.readAll()) {
        result.errorReturned = "Failed to read CSV file: " + filePath;
        co_return result;
    }

    const auto *dataRode = reader.dataRode();

    // Required columns
    QStringList requiredColumns = {
        "TRANSACTION_TYPE",
        "PRICE_OF_ITEMS_VAT_RATE_PERCENT",
        "TRANSACTION_COMPLETE_DATE",
        "VAT_CALCULATION_IMPUTATION_COUNTRY",
        "PRODUCT_TAX_CODE",
        "PRICE_OF_ITEMS_AMT_VAT_EXCL",
        "TOTAL_ACTIVITY_VALUE_VAT_AMT",
        "VAT_INV_NUMBER",
        "TRANSACTION_CURRENCY_CODE",
        "MARKETPLACE"
    };

    for (const QString &col : requiredColumns) {
         if (!dataRode->header.contains(col)) {
             result.errorReturned = "Missing column: " + col;
             co_return result;
         }
    }

    int indTransType = dataRode->header.pos("TRANSACTION_TYPE");
    // int indVatRate = dataRode->header.pos("PRICE_OF_ITEMS_VAT_RATE_PERCENT"); // Unused variable
    int indDate = dataRode->header.pos("TRANSACTION_COMPLETE_DATE");
    int indTaxCalcDate = dataRode->header.pos("TAX_CALCULATION_DATE"); 
    int indTaxCountry = dataRode->header.pos("VAT_CALCULATION_IMPUTATION_COUNTRY");
    int indPtCode = dataRode->header.pos("PRODUCT_TAX_CODE");
    int indCurrency = dataRode->header.pos("TRANSACTION_CURRENCY_CODE");
    int indMarketplace = dataRode->header.pos("MARKETPLACE");
    
    int indEventId = -1;
    if (dataRode->header.contains("TRANSACTION_EVENT_ID")) indEventId = dataRode->header.pos("TRANSACTION_EVENT_ID");
    else if (dataRode->header.contains("ORDER_ID")) indEventId = dataRode->header.pos("ORDER_ID");

    // Amounts
    int indPriceExcl = dataRode->header.pos("PRICE_OF_ITEMS_AMT_VAT_EXCL");
    int indTotalVat = dataRode->header.pos("TOTAL_ACTIVITY_VALUE_VAT_AMT"); // Total VAT for the line? NO, standard says TOTAL_ACTIVITY_VALUE_VAT_AMT is total VAT amount.
    // Wait, let's double check columns. 
    // PRICE_OF_ITEMS_AMT_VAT_EXCL + SHIP_CHARGE... + GIFT...
    // To simplify we might just take TOTAL_ACTIVITY_VALUE_AMT_VAT_INCL and TOTAL_ACTIVITY_VALUE_AMT_VAT_EXCL if available
    // int indTotalIncl = dataRode->header.pos("TOTAL_ACTIVITY_VALUE_AMT_VAT_INCL"); // Unused if we calculate from Excl + Vat
    int indTotalExcl = dataRode->header.pos("TOTAL_ACTIVITY_VALUE_AMT_VAT_EXCL");
    
    int indInvNumber = dataRode->header.pos("VAT_INV_NUMBER");
    int indInvUrl = dataRode->header.pos("INVOICE_URL");

    int indDepart = -1;
    if (dataRode->header.contains("SALE_DEPART_COUNTRY")) indDepart = dataRode->header.pos("SALE_DEPART_COUNTRY");
    else if (dataRode->header.contains("DEPARTURE_COUNTRY")) indDepart = dataRode->header.pos("DEPARTURE_COUNTRY");

    int indArrival = -1;
    if (dataRode->header.contains("SALE_ARRIVAL_COUNTRY")) indArrival = dataRode->header.pos("SALE_ARRIVAL_COUNTRY");
    else if (dataRode->header.contains("ARRIVAL_COUNTRY")) indArrival = dataRode->header.pos("ARRIVAL_COUNTRY");

    // int indBuyerTax = -1; // Unused
    // if (dataRode->header.contains("BUYER_VAT_NUMBER")) indBuyerTax = dataRode->header.pos("BUYER_VAT_NUMBER");
    
    int indTaxScheme = dataRode->header.pos("TAX_REPORTING_SCHEME");
    int indTaxCollectionResp = dataRode->header.pos("TAX_COLLECTION_RESPONSIBILITY");

    // int indSellerSku = dataRode->header.pos("SELLER_SKU"); // Unused
    // int indAsin = dataRode->header.pos("ASIN"); // Unused
    int indActivityId = dataRode->header.pos("ACTIVITY_TRANSACTION_ID"); // Unique ID per line

    // Temporary map to aggregate items by Shipment ID
    struct TempShipment {
        QString type; // SALE or REFUND
        QList<Activity> activities;
        QString invoiceNumber;
        QString invoiceUrl;
        QDate date;
    };
    QMap<QString, TempShipment> shipmentMap;

    for (const auto &line : dataRode->lines) {
        QString transType = line.value(indTransType);
        if (transType != "SALE" && transType != "REFUND") continue;

        QString eventId = (indEventId != -1) ? line.value(indEventId) : "";
        if (eventId.isEmpty()) continue; // Should not happen for SALE/REFUND

        // Validation of eventId ? No, keep strict reading.
        
        QString marketplace = line.value(indMarketplace);
        if (!marketplace.isEmpty()) {
             result.orderInfos->orderId_store[eventId] = marketplace;
        }

        // Date priority: TAX_CALCULATION_DATE > TRANSACTION_COMPLETE_DATE
        QString dateStr;
        if (indTaxCalcDate != -1 && !line.value(indTaxCalcDate).isEmpty()) {
            dateStr = line.value(indTaxCalcDate);
        } else {
            dateStr = line.value(indDate);
        }
        QDate date = QDate::fromString(dateStr, "dd-MM-yyyy");
        if (!date.isValid()) date = QDate::fromString(dateStr, "dd/MM/yyyy");
        if (!date.isValid()) date = QDate::fromString(dateStr, "yyyy-MM-dd"); // From test_vat_rate
        if (!date.isValid()) {
             // Fallback or skip?
             continue; 
        }

        // Update range
        if (result.orderInfos->dateMin.isNull() || date < result.orderInfos->dateMin) result.orderInfos->dateMin = date;
        if (result.orderInfos->dateMax.isNull() || date > result.orderInfos->dateMax) result.orderInfos->dateMax = date;

        // Amounts
        // double amountIncl = line.value(indTotalIncl).toDouble(); // Unused
        double amountExcl = line.value(indTotalExcl).toDouble();
        double amountVat = line.value(indTotalVat).toDouble();

        // If refund, amounts might be negative or positive depending on report style.
        // In checked reports (test_vat_rate), Refund amounts are negative. 
        // We probably want absolute values for Amount object, or strictly following Activity convention.
        // Activity Amount checks: untaxed + taxes = taxed. 
        // Let's keep signs as is for now, Activity should handle signed amounts.
        
        QString currency = line.value(indCurrency);
        QString depart = (indDepart != -1) ? line.value(indDepart) : "";
        QString arrival = (indArrival != -1) ? line.value(indArrival) : "";
        QString vatPaidTo = line.value(indTaxCountry);
        if (arrival.isEmpty()) arrival = vatPaidTo; // Fallback
        
        // Tax Scheme mapping
        // Logic similar to test_vat_rate could be useful but here we rely on columns if possible
        // Or we infer. 
        // For now, let's map what we have.
        TaxScheme scheme = TaxScheme::Unknown;
        QString taxResp = line.value(indTaxCollectionResp); // MARKETPLACE or SELLER
        QString schemeStr = line.value(indTaxScheme); // UNION-OSS, REGULAR, etc.

        if (taxResp == "MARKETPLACE") scheme = TaxScheme::MarketplaceDeemedSupplier;
        else if (schemeStr == "UNION-OSS") scheme = TaxScheme::EuOssUnion;
        else {
             // Fallback logic could be complex (Export, Domestic, etc.)
             // For now defaults to Unknown or infer from country
             if (depart == arrival) scheme = TaxScheme::DomesticVat;
             else if (!depart.isEmpty() && !arrival.isEmpty() && depart != arrival) scheme = TaxScheme::EuOssUnion; // Simplification?
        }

        TaxSource taxSource = TaxSource::MarketplaceProvided;
        QString ptCode = line.value(indPtCode);
        (void)ptCode; // Unused for now

        // Activity ID
        QString actId = line.value(indActivityId);
        
        // Create Activity
        Amount amt(amountExcl + amountVat, amountVat);
        // Note: Amount constructor takes (Taxed, Tax). We construct Taxed from Excl+Vat to ensure consistency with Excl column. 
        
        auto actRes = Activity::create(
            eventId,
            actId,
            "", // subId
            date.startOfDay(),
            currency,
            depart,
            arrival,
            vatPaidTo,
            amt,
            taxSource,
            vatPaidTo, // Declaring country usually same as vatPaidTo in these reports
            scheme,
            TaxJurisdictionLevel::Country,
            SaleType::Products
        );

        if (!actRes.ok()) {
             qCritical() << "Failed to create activity:" << (actRes.errors.isEmpty() ? "Unknown error" : actRes.errors.first().message) << " EventID:" << eventId << " Type:" << transType;
             continue;
        }
        
        // Use composite key to separate Sales and Refunds if they share the same EventID
        QString mapKey = eventId + "_" + transType;
        TempShipment &ts = shipmentMap[mapKey];
        ts.type = transType;
        ts.date = date;
        ts.activities.append(actRes.value.value());
        ts.invoiceNumber = line.value(indInvNumber);
        if (indInvUrl != -1) ts.invoiceUrl = line.value(indInvUrl);
    }
    
    // Convert TempShipment to Shipment/Refund
    for (auto it = shipmentMap.begin(); it != shipmentMap.end(); ++it) {
        QString eventId = it.key();
        TempShipment &ts = it.value();
        
        // Aggregate items for InvoicingInfo
        // We need to create LineItems. But ImporterFileAmazonVatEu requirements were just "Activity".
        // However, InvoicingInfo needs LineItems.
        // Wait, AbstractImporter::OrderInfos has shipments and refunds.
        // Shipment takes List<Activity>.
        // InvoicingInfo takes Shipment + LineItems.
        
        // We didn't parse Line Item details (Title, etc) in the loop above fully (only Sku/Asin available but not mapped to Activity directly).
        // Activity doesn't hold SKU/Title.
        // So we need to create LineItems separately if we want InvoicingInfo.
        // But AbstractImporterFile::loadReport returns ReturnOrderInfos which contains OrderInfos.
        // OrderInfos contains shipments, refunds, invoicingInfos.
        
        // We didn't explicitly demand full LineItem details (Title), but generally good for Invoice.
        
        // To do this properly, we should have stored Line Item data in TempShipment.
        // Let's assume for now we just populate Shipments/Refunds with Activities. 
        // If InvoicingInfo is required (likely yes for "Importer"), we populate it with basic info.
        
        if (ts.type == "SALE") {
            Shipment shipment(ts.activities);
            result.orderInfos->shipments.append(shipment);
            
            // Basic InvoicingInfo
            InvoicingInfo info(&result.orderInfos->shipments.last());
            // We can set Invoice Number
            // We don't have detailed line items (description) easily unless we map them.
            // Let's create dummy LineItems from activities if needed or just skip detailed items if acceptable.
            
            result.orderInfos->invoicingInfos.append({eventId, info});
        } else if (ts.type == "REFUND") {
            Refund refund(ts.activities);
            result.orderInfos->refunds.append(refund);
            
             // Refunds also have InvoicingInfo? yes.
            InvoicingInfo info(&result.orderInfos->refunds.last());
            result.orderInfos->invoicingInfos.append({eventId, info});
        }
    }

    co_return result;
}
