#include "ImporterApiAmazonEu.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QTimer>
#include <QTemporaryFile>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QCoroNetworkReply>
#include <QCoroSignal>
#include "utils/CsvReader.h"

DECLARE_IMPORTER_API(ImporterApiAmazonEu)

QString ImporterApiAmazonEu::getEndpoint() const
{
    return "https://sellingpartnerapi-eu.amazon.com";
}

QString ImporterApiAmazonEu::getLabel() const
{
    return "Amazon SP-API (EU)";
}

QString ImporterApiAmazonEu::getId() const
{
    return "AmazonSpApiEu";
}

QString ImporterApiAmazonEu::getRegion() const
{
    return "eu-west-1";
}

QString ImporterApiAmazonEu::getMarketplaceId() const
{
    // Default to DE marketplace, likely should be configurable or iterated?
    // User request implies simple classes. Let's stick to DE for now or all EU marketplaces?
    // "MarketplaceIds" is a list in Orders API.
    // For now, hardcoding DE as primary example or allowing param override.
    return "A1PA6795UKMFR9"; // Germany
}

QCoro::Task<void> ImporterApiAmazonEu::_populateMovedUnits(const QDateTime &dateFrom, OrderInfos *orderInfos)
{
    // 1. Request the VAT Transaction Data report which includes FC_TRANSFER rows
    QJsonObject bodyObj;
    bodyObj["reportType"] = "GET_VAT_TRANSACTION_DATA";
    bodyObj["marketplaceIds"] = QJsonArray{getMarketplaceId()};
    bodyObj["dataStartTime"] = dateFrom.toUTC().toString(Qt::ISODate);

    QByteArray createResp = co_await sendSignedRequest(
        "POST", "/reports/2021-06-30/reports", {},
        QJsonDocument(bodyObj).toJson(QJsonDocument::Compact));

    QString reportId = QJsonDocument::fromJson(createResp).object()["reportId"].toString();
    if (reportId.isEmpty()) co_return;

    // 2. Poll until the report is ready (max 12 attempts × 5 s = 1 min)
    QString reportDocumentId;
    for (int attempt = 0; attempt < 12; ++attempt) {
        QTimer pollTimer;
        pollTimer.setSingleShot(true);
        pollTimer.start(5000);
        co_await qCoro(&pollTimer, &QTimer::timeout);

        QByteArray statusResp = co_await sendSignedRequest(
            "GET", "/reports/2021-06-30/reports/" + reportId, {});
        QJsonObject statusObj = QJsonDocument::fromJson(statusResp).object();
        QString status = statusObj["processingStatus"].toString();

        if (status == "DONE") {
            reportDocumentId = statusObj["reportDocumentId"].toString();
            break;
        }
        if (status == "FATAL" || status == "CANCELLED") co_return;
    }
    if (reportDocumentId.isEmpty()) co_return;

    // 3. Retrieve the pre-signed S3 download URL
    QByteArray docResp = co_await sendSignedRequest(
        "GET", "/reports/2021-06-30/documents/" + reportDocumentId, {});
    QString downloadUrl = QJsonDocument::fromJson(docResp).object()["url"].toString();
    if (downloadUrl.isEmpty()) co_return;

    // 4. Download the CSV from the pre-signed S3 URL (no SigV4 needed)
    QNetworkAccessManager plainNam;
    QNetworkRequest plainReq{QUrl(downloadUrl)};
    QNetworkReply *downloadReply = plainNam.get(plainReq);
    co_await downloadReply;
    if (downloadReply->error() != QNetworkReply::NoError) {
        downloadReply->deleteLater();
        co_return;
    }
    QByteArray reportData = downloadReply->readAll();
    downloadReply->deleteLater();

    // 5. Write to a temp file and parse with CsvReader
    QTemporaryFile tmpFile;
    if (!tmpFile.open()) co_return;
    tmpFile.write(reportData);
    tmpFile.flush();

    CsvReader csvReader(tmpFile.fileName(), ",", "\"", true, "\n", 0, "UTF-8");
    if (!csvReader.readAll()) co_return;
    const auto *data = csvReader.dataRode();

    int indTransType = data->header.contains("TRANSACTION_TYPE")          ? data->header.pos("TRANSACTION_TYPE")          : -1;
    int indEventId   = data->header.contains("TRANSACTION_EVENT_ID")      ? data->header.pos("TRANSACTION_EVENT_ID")      : -1;
    int indSku       = data->header.contains("SELLER_SKU")                ? data->header.pos("SELLER_SKU")                : -1;
    int indQty       = data->header.contains("QTY")                       ? data->header.pos("QTY")                       : -1;
    int indDepart    = data->header.contains("DEPARTURE_COUNTRY")         ? data->header.pos("DEPARTURE_COUNTRY")         : -1;
    int indArrival   = data->header.contains("ARRIVAL_COUNTRY")           ? data->header.pos("ARRIVAL_COUNTRY")           : -1;
    int indDate      = data->header.contains("TRANSACTION_COMPLETE_DATE") ? data->header.pos("TRANSACTION_COMPLETE_DATE") : -1;

    if (indTransType < 0 || indEventId < 0 || indSku < 0 || indQty < 0 || indDepart < 0 || indArrival < 0 || indDate < 0) co_return;

    for (const auto &line : data->lines) {
        if (line.value(indTransType) != "FC_TRANSFER") {
            continue;
        }
        QString eventId = line.value(indEventId);
        QString sku     = line.value(indSku);
        int     qty     = line.value(indQty).toInt();
        QString from    = line.value(indDepart);
        QString to      = line.value(indArrival);
        QDate   date;
        for (const QString &fmt : {"dd-MM-yyyy", "dd/MM/yyyy", "yyyy-MM-dd"}) {
            date = QDate::fromString(line.value(indDate), fmt);
            if (date.isValid()) break;
        }
        if (!eventId.isEmpty() && !sku.isEmpty() && qty > 0 && !from.isEmpty() && !to.isEmpty() && from != to && date.isValid()) {
            orderInfos->year_month_countryFrom_countryTo_eventId_sku_units[date.year()][date.month()][from][to][eventId][sku] += qty;
        }
    }
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiAmazonEu::_fetchShipments(const QDateTime &dateFrom)
{
    ReturnOrderInfos result;
    result.orderInfos = QSharedPointer<OrderInfos>::create();

    const QString path = "/orders/v0/orders";

    QUrlQuery query;
    query.addQueryItem("MarketplaceIds", getMarketplaceId());
    query.addQueryItem("CreatedAfter", dateFrom.toUTC().toString(Qt::ISODate));

    try {
        bool hasMore = true;
        while (hasMore) {
            QByteArray response = co_await sendSignedRequest("GET", path, query);
            const QJsonObject root = QJsonDocument::fromJson(response).object();

            if (!root.contains("payload")) {
                result.errorReturned = "Invalid response format: missing payload";
                break;
            }

            const QJsonObject payload = root["payload"].toObject();
            const QJsonArray orders = payload["Orders"].toArray();

            for (const QJsonValue &val : std::as_const(orders)) {
                const QJsonObject order = val.toObject();
                const QString orderId = order["AmazonOrderId"].toString();
                if (orderId.isEmpty()) {
                    continue;
                }
                result.orderInfos->orderId_infos[orderId] =
                    OrderManager::OrderInfo{getMarketplaceId(), isGroupedOrders(), ""};
            }

            // Per SP-API spec, when paging with NextToken only pass NextToken
            const QString nextToken = payload["NextToken"].toString();
            if (nextToken.isEmpty() || orders.isEmpty()) {
                hasMore = false;
            } else {
                query.clear();
                query.addQueryItem("NextToken", nextToken);
            }
        }
    } catch (const std::exception& e) {
        result.errorReturned = QString::fromStdString(e.what());
    }

    // Populate moved units from FC_TRANSFER rows in the VAT transaction report (non-fatal)
    try {
        co_await _populateMovedUnits(dateFrom, result.orderInfos.data());
    } catch (const std::exception& e) {
        qWarning() << "ImporterApiAmazonEu: failed to populate moved units:" << e.what();
    }

    co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiAmazonEu::_fetchRefunds(const QDateTime &dateFrom)
{
    ReturnOrderInfos result;
    result.orderInfos = QSharedPointer<OrderInfos>::create();

    const QString path = "/finances/v0/financialEvents";
    QUrlQuery query;
    query.addQueryItem("PostedAfter", dateFrom.toUTC().toString(Qt::ISODate));

    try {
        QByteArray response = co_await sendSignedRequest("GET", path, query);
        const QJsonObject root = QJsonDocument::fromJson(response).object();

        if (root.contains("payload")) {
            const QJsonObject payload = root["payload"].toObject();
            const QJsonObject events = payload["FinancialEvents"].toObject();
            const QJsonArray refundEvents = events["RefundEventList"].toArray();

            for (const QJsonValue &val : std::as_const(refundEvents)) {
                const QJsonObject ev = val.toObject();
                const QString orderId = ev["AmazonOrderId"].toString();
                if (orderId.isEmpty()) {
                    continue;
                }

                const QDate date = QDate::fromString(
                    ev["PostedDate"].toString().left(10), "yyyy-MM-dd");

                // Sum Principal charges across all shipment item adjustments.
                // Tax is not included: for EU, Amazon reports net principal separately;
                // for NA, Amazon is the marketplace facilitator and tax is not the seller's.
                double principal = 0.0;
                QString currency;
                const QJsonArray items = ev["ShipmentItemAdjustmentList"].toArray();
                for (const QJsonValue &itemVal : std::as_const(items)) {
                    const QJsonArray charges =
                        itemVal.toObject()["ItemChargeAdjustmentList"].toArray();
                    for (const QJsonValue &chargeVal : std::as_const(charges)) {
                        const QJsonObject charge = chargeVal.toObject();
                        if (charge["ChargeType"].toString() == "Principal") {
                            const QJsonObject amt = charge["ChargeAmount"].toObject();
                            principal += amt["Amount"].toString().toDouble();
                            if (currency.isEmpty()) {
                                currency = amt["CurrencyCode"].toString();
                            }
                        }
                    }
                }

                if (!qFuzzyIsNull(principal)) {
                    result.orderInfos->orderId_refundClue[orderId] =
                        {qAbs(principal), currency, date};
                }
            }
        }
    } catch (const std::exception& e) {
        result.errorReturned = QString::fromStdString(e.what());
    }

    co_return result;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiAmazonEu::_fetchAddresses(const QDateTime &dateFrom)
{
    // Amazon restricts PII (Personally Identifiable Information).
    // Getting addresses requires RDT (Restricted Data Token).
    // This adds significant complexity (another token exchange type).
    // For this task, we might skip address fetching or return empty with a note/log?
    // Or we assume we have RDT? 
    // Standard LWA token might not work for PII. 
    
    ReturnOrderInfos result;
    result.orderInfos = QSharedPointer<OrderInfos>::create();
    // Leaving empty as PII requires Restricted Data Token which is out of scope for basic implementation
    co_return result; 
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterApiAmazonEu::_fetchInvoiceInfos(const QDateTime &dateFrom)
{
     // Not available directly via simple API, often requires Reports API or feed.
     ReturnOrderInfos result;
     result.orderInfos = QSharedPointer<OrderInfos>::create();
     co_return result;
}

