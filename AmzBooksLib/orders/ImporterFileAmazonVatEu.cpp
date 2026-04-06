#include "ImporterFileAmazonVatEu.h"
#include "orders/LineItem.h"
#include "utils/CsvReader.h"
#include "CountriesEu.h"
#include <QFileInfo>
#include <QDebug>

DECLARE_IMPORTER_FILE(ImporterFileAmazonVatEu)

QString ImporterFileAmazonVatEu::getLabel() const
{
    return QObject::tr("Amazon EU VAT Report");
}

ActivitySource ImporterFileAmazonVatEu::getActivitySource() const
{
    return {ActivitySourceType::Report, CHANNEL_AMAZON, "Amazon EU", QObject::tr("VAT Report")};
}

QString ImporterFileAmazonVatEu::getId() const
{
    return "AmazonVatEu";
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

bool ImporterFileAmazonVatEu::recomputeTaxes() const
{
    return false;
}

bool ImporterFileAmazonVatEu::isWrongIfConflict() const
{
    return false;
}

bool ImporterFileAmazonVatEu::fixRefundDate() const
{
    return false;
}

bool ImporterFileAmazonVatEu::isGroupedOrders() const
{
    return true;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterFileAmazonVatEu::_loadReport(
    const QString &filePath,
    std::function<QCoro::Task<bool>(const QString &errorTitle, const QString &errorText)> callbackAddIfMissing)
{
    Q_UNUSED(callbackAddIfMissing)
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
    if (dataRode->header.contains("TRANSACTION_EVENT_ID")) {
        indEventId = dataRode->header.pos("TRANSACTION_EVENT_ID");
    } else if (dataRode->header.contains("ORDER_ID")) {
        indEventId = dataRode->header.pos("ORDER_ID");
    }

    // Amounts
    int indTotalVat = dataRode->header.pos("TOTAL_ACTIVITY_VALUE_VAT_AMT"); // Total VAT for the line? NO, standard says TOTAL_ACTIVITY_VALUE_VAT_AMT is total VAT amount.
    // Wait, let's double check columns.
    // PRICE_OF_ITEMS_AMT_VAT_EXCL + SHIP_CHARGE... + GIFT...
    // To simplify we might just take TOTAL_ACTIVITY_VALUE_AMT_VAT_INCL and TOTAL_ACTIVITY_VALUE_AMT_VAT_EXCL if available
    // int indTotalIncl = dataRode->header.pos("TOTAL_ACTIVITY_VALUE_AMT_VAT_INCL"); // Unused if we calculate from Excl + Vat
    int indTotalExcl = dataRode->header.pos("TOTAL_ACTIVITY_VALUE_AMT_VAT_EXCL");
    int indVatRate = dataRode->header.pos("PRICE_OF_ITEMS_VAT_RATE_PERCENT");

    int indInvNumber = dataRode->header.pos("VAT_INV_NUMBER");
    int indInvUrl = dataRode->header.pos("INVOICE_URL");
    int indItemDesc = dataRode->header.contains("ITEM_DESCRIPTION") ? dataRode->header.pos("ITEM_DESCRIPTION") : -1;

    int indDepart = -1;
    if (dataRode->header.contains("SALE_DEPART_COUNTRY")) indDepart = dataRode->header.pos("SALE_DEPART_COUNTRY");
    else if (dataRode->header.contains("DEPARTURE_COUNTRY")) indDepart = dataRode->header.pos("DEPARTURE_COUNTRY");

    int indArrival = -1;
    if (dataRode->header.contains("SALE_ARRIVAL_COUNTRY")) indArrival = dataRode->header.pos("SALE_ARRIVAL_COUNTRY");
    else if (dataRode->header.contains("ARRIVAL_COUNTRY")) indArrival = dataRode->header.pos("ARRIVAL_COUNTRY");

    int indArrivalPostCode = dataRode->header.contains("ARRIVAL_POST_CODE")
                             ? dataRode->header.pos("ARRIVAL_POST_CODE") : -1;

    // int indBuyerTax = -1; // Unused
    // if (dataRode->header.contains("BUYER_VAT_NUMBER")) indBuyerTax = dataRode->header.pos("BUYER_VAT_NUMBER");
    
    int indTaxScheme = dataRode->header.pos("TAX_REPORTING_SCHEME");
    int indTaxCollectionResp = dataRode->header.pos("TAX_COLLECTION_RESPONSIBILITY");

    int indSellerSku = dataRode->header.pos("SELLER_SKU");
    int indQty = dataRode->header.pos("QTY");
    // Physical departure/arrival countries (distinct from SALE_DEPART/ARRIVAL_COUNTRY which are empty for FC_TRANSFER)
    int indDepartCountry = dataRode->header.contains("DEPARTURE_COUNTRY") ? dataRode->header.pos("DEPARTURE_COUNTRY") : -1;
    int indArrivalCountry = dataRode->header.contains("ARRIVAL_COUNTRY") ? dataRode->header.pos("ARRIVAL_COUNTRY") : -1;
    int indActivityId = dataRode->header.pos("ACTIVITY_TRANSACTION_ID"); // Unique ID per line

    // Temporary map to aggregate items by Shipment ID (actId)
    struct TempShipment {
        QString eventId; // Activity ID (used as shipmentOrRefundId for invoicingInfos)
        QString type; // SALE or REFUND
        QList<Activity> activities;
        QString invoiceNumber;
        QString invoiceUrl;
        QDate date;
        // Line-item data for building InvoicingInfo even when no Amazon invoice number
        QString itemSku;
        QString itemDescription;
        int itemQty = 1;
        double itemVatRate = 0.0;
        double itemTotalTaxed = 0.0;
    };
    QMap<QString, TempShipment> shipmentMap;

    for (const auto &line : dataRode->lines) {
        QString transType = line.value(indTransType);

        if (transType == "FC_TRANSFER") {
            if (indSellerSku >= 0 && indQty >= 0 && indDepartCountry >= 0 && indArrivalCountry >= 0) {
                QString eventId = (indEventId >= 0) ? line.value(indEventId) : QString();
                QString sku  = line.value(indSellerSku);
                int     qty  = line.value(indQty).toInt();
                QString from = line.value(indDepartCountry);
                QString to   = line.value(indArrivalCountry);
                QDate   date = parseDateFormats(line.value(indDate), {"dd-MM-yyyy", "dd/MM/yyyy", "yyyy-MM-dd"});
                if (!eventId.isEmpty() && !sku.isEmpty() && qty > 0 && !from.isEmpty() && !to.isEmpty() && from != to && date.isValid()) {
                    result.orderInfos->year_month_countryFrom_countryTo_eventId_sku_units[date.year()][date.month()][from][to][eventId][sku] += qty;
                    if (result.orderInfos->dateMin.isNull() || date < result.orderInfos->dateMin)
                        result.orderInfos->dateMin = date;
                    if (result.orderInfos->dateMax.isNull() || date > result.orderInfos->dateMax)
                        result.orderInfos->dateMax = date;
                }
            }
            continue;
        }

        if (transType != "SALE" && transType != "REFUND") continue;

        QString eventId = (indEventId != -1) ? line.value(indEventId) : "";
        if (eventId.isEmpty()) continue; // Should not happen for SALE/REFUND

        // Validation of eventId ? No, keep strict reading.
        
        QString marketplace = line.value(indMarketplace);

        QString dateStr = line.value(indDate);
        QDate date = parseDateFormats(dateStr, {"dd-MM-yyyy", "dd/MM/yyyy", "yyyy-MM-dd"});
        if (!date.isValid()) {
             // Fallback or skip?
             continue; 
        }

        // Update range
        if (result.orderInfos->dateMin.isNull() || date < result.orderInfos->dateMin) {
            result.orderInfos->dateMin = date;
        }
        if (result.orderInfos->dateMax.isNull() || date > result.orderInfos->dateMax) {
            result.orderInfos->dateMax = date;
        }

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

        // Northern Ireland postcodes start with "BT". Amazon reports the country as
        // GB but NI remains in the EU VAT area under the NI Protocol → use XI.
        if (arrival == "GB" && indArrivalPostCode != -1) {
            const QString &arrivalPostCode = line.value(indArrivalPostCode);
            if (arrivalPostCode.startsWith("BT", Qt::CaseInsensitive)) {
                arrival = CountriesEu::XI;
            }
        }
        
        // Tax Scheme mapping
        // Logic similar to test_vat_rate could be useful but here we rely on columns if possible
        // Or we infer. 
        // For now, let's map what we have.
        TaxScheme scheme = TaxScheme::Unknown;
        QString taxResp = line.value(indTaxCollectionResp); // MARKETPLACE or SELLER
        QString schemeStr = line.value(indTaxScheme); // UNION-OSS, REGULAR, etc.

        if (schemeStr == "UNION-OSS") {
            scheme = TaxScheme::EuOssUnion;
        } else if (schemeStr == "REGULAR") {
            scheme = TaxScheme::DomesticVat;
        } else if (schemeStr == "IMPORT-OSS") {
            scheme = TaxScheme::EuIoss;
        } else if (schemeStr == "DEEMED_RESELLER-IOSS") {
            scheme = TaxScheme::EuIoss;
        } else if (schemeStr == "UK_VOEC-IMPORT") {
            scheme = TaxScheme::Exempt; // Export to UK (Tax collected by Amazon)
        } else if (schemeStr == "UK_VOEC-DOMESTIC") {
            scheme = TaxScheme::Exempt; // Export to UK (Tax collected by Amazon or domestic UK)
        } else if (schemeStr == "CH_VOEC") {
            scheme = TaxScheme::Exempt; // Export to CH (Tax collected by Amazon)
        } else if (schemeStr == "COMMINGLE_VAT") {
            scheme = TaxScheme::DomesticVat; // Treat commingling as domestic storage/sale logic usually
        }
        // When Amazon is the deemed supplier it collects and remits the VAT itself.
        // The seller receives gross revenue with 0 VAT — treat as MarketplaceDeemedSupplier
        // regardless of what the scheme string says.
        if (taxResp == "MARKETPLACE") {
            scheme = TaxScheme::MarketplaceDeemedSupplier;
        }
        if (scheme == TaxScheme::EuOssUnion || scheme == TaxScheme::DomesticVat) {
            if (qAbs(amountVat) < 0.001
                && line.value(indInvNumber).isEmpty()
                && transType == "SALE") {
                continue; // VINE order not charged
            }
        }

        if (!marketplace.isEmpty()) {
            result.orderInfos->orderId_infos[eventId] = OrderManager::OrderInfo{marketplace, isGroupedOrders(), ""};
        }

        /*
    if (schemeStr == "NO_VOEC") scheme = TaxScheme::Exempt;
        if (taxResp == "MARKETPLACE") {
            scheme = TaxScheme::MarketplaceDeemedSupplier;
        } else if (schemeStr == "UNION-OSS") {
            scheme = TaxScheme::EuOssUnion;
        } else {
             // Fallback logic could be complex (Export, Domestic, etc.)
             // For now defaults to Unknown or infer from country
             if (depart == arrival) {
                 scheme = TaxScheme::DomesticVat;
             } else if (!depart.isEmpty()
                        && !arrival.isEmpty()
                        && depart != arrival) {
                 scheme = TaxScheme::EuOssUnion; // Simplification?
             }
        }
//*/

        TaxSource taxSource = TaxSource::MarketplaceProvided;
        QString ptCode = line.value(indPtCode);
        (void)ptCode; // Unused for now

        // Activity ID
        QString actId = line.value(indActivityId);
        
        // Create Activity
        ::Amount amt(amountExcl + amountVat, amountVat);
        // Note: Amount constructor takes (Taxed, Tax). We construct Taxed from Excl+Vat to ensure consistency with Excl column. 
        
        // Date parsing for TAX_CALCULATION_DATE
        QString dateTaxStr = (indTaxCalcDate != -1) ? line.value(indTaxCalcDate) : "";
        QDate dateTax = parseDateFormats(dateTaxStr, {"dd-MM-yyyy", "dd/MM/yyyy", "yyyy-MM-dd"});
        if (!dateTax.isValid()) dateTax = date; // Fallback to transaction date if missing
        
        auto actRes = Activity::create(
            eventId,
            actId,
            "", // subId
            date.startOfDay(),
            dateTax.startOfDay(),
            currency,
            depart,
            arrival,
            false, // isCompany (not determinable from VAT report; defaulting to B2C)
            vatPaidTo,
            amt,
            taxSource,
            vatPaidTo, // Declaring country usually same as vatPaidTo in these reports
            scheme,
            TaxJurisdictionLevel::Country,
            SaleType::Products,
            "", "", // Territories
            line.value(indInvNumber) // Invoice ID
        );

        if (!actRes.ok()) {
             qCritical() << "Failed to create activity:" << (actRes.errors.isEmpty() ? "Unknown error" : actRes.errors.first().message) << " EventID:" << eventId << " Type:" << transType;
             continue;
        }
        
        // Use actId as the map key — it's the true shipment/refund ID.
        // eventId is the order ID, not the shipment ID.
        QString mapKey = actId + "_" + transType;
        TempShipment &ts = shipmentMap[mapKey];
        ts.eventId = actId;
        ts.type = transType;
        ts.date = date;
        ts.activities.append(actRes.value.value());
        ts.invoiceNumber = line.value(indInvNumber);
        if (indInvUrl != -1) {
            ts.invoiceUrl = line.value(indInvUrl);
        }
        // Accumulate line-item data for InvoicingInfo construction
        if (ts.itemSku.isEmpty() && indSellerSku >= 0)
            ts.itemSku = line.value(indSellerSku);
        if (ts.itemDescription.isEmpty() && indItemDesc >= 0)
            ts.itemDescription = line.value(indItemDesc);
        if (indQty >= 0)
            ts.itemQty = qMax(1, line.value(indQty).toInt());
        ts.itemVatRate = line.value(indVatRate).toDouble();
        ts.itemTotalTaxed += (amountExcl + amountVat);
    }
    
    // Convert TempShipment to Shipment/Refund
    for (auto it = shipmentMap.begin(); it != shipmentMap.end(); ++it) {
        TempShipment &ts = it.value();
        const QString &eventId = ts.eventId;

        // Build a LineItem from the per-row data collected during parsing.
        // This ensures InvoicingInfo::create() always succeeds even when there is no
        // Amazon invoice number or URL (e.g. refunds without a VAT_INV_NUMBER).
        QList<LineItem> items;
        {
            const QString itemName = ts.itemDescription.isEmpty() ? ts.itemSku : ts.itemDescription;
            if (!itemName.isEmpty() && ts.itemTotalTaxed != 0.0 && ts.itemQty > 0) {
                auto lineItemRes = LineItem::create(
                    ts.itemSku, itemName,
                    ts.itemTotalTaxed / ts.itemQty,
                    ts.itemVatRate,
                    ts.itemQty);
                if (lineItemRes.ok())
                    items.append(*lineItemRes.value);
            }
        }

        // Pass nullopt (not empty string) so toJson() omits the "invoiceNumber" key entirely.
        // The no-invoice SQL filter detects missing invoices via the absence of "invoiceNumber" in JSON;
        // storing an empty string would write "invoiceNumber":"" and fool that filter.
        std::optional<QString> optInvNumber = ts.invoiceNumber.isEmpty() ? std::nullopt : std::optional<QString>(ts.invoiceNumber);
        std::optional<QString> optInvUrl    = ts.invoiceUrl.isEmpty()    ? std::nullopt : std::optional<QString>(ts.invoiceUrl);

        if (ts.type == "SALE") {
            Shipment shipment(ts.activities, "", isGroupedOrders());
            result.orderInfos->shipments.append(shipment);

            auto infoRes = InvoicingInfo::create(&result.orderInfos->shipments.last(), items, optInvNumber, optInvUrl, ts.date);
            if (infoRes.ok()) {
                result.orderInfos->invoicingInfos.append({eventId, *infoRes.value});
            } else {
                QString err = infoRes.errors.isEmpty() ? "Unknown error" : infoRes.errors.first().message;
                qWarning() << "InvoicingInfo creation failed for SALE" << eventId << ":" << err;
            }

        } else if (ts.type == "REFUND") {
            Refund refund(ts.activities, "", isGroupedOrders());
            result.orderInfos->refunds.append(refund);

            auto infoRes = InvoicingInfo::create(&result.orderInfos->refunds.last(), items, optInvNumber, optInvUrl, ts.date);
            if (infoRes.ok()) {
                result.orderInfos->invoicingInfos.append({eventId, *infoRes.value});
            } else {
                QString err = infoRes.errors.isEmpty() ? "Unknown error" : infoRes.errors.first().message;
                qWarning() << "InvoicingInfo creation failed for REFUND" << eventId << ":" << err;
            }
        }
    }

    co_return result;
}
