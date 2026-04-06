#include "ImporterFileAmazonTransactions.h"
#include "utils/CsvReader.h"
#include "utils/CsvHeader.h"
#include <QFileInfo>
#include <QDebug>

DECLARE_IMPORTER_FILE(ImporterFileAmazonTransactions)

QString ImporterFileAmazonTransactions::getLabel() const
{
    return QObject::tr("Amazon Transactions Report");
}

ActivitySource ImporterFileAmazonTransactions::getActivitySource() const
{
    return {ActivitySourceType::Report, CHANNEL_AMAZON, QString{}, QObject::tr("Transactions Report")};
}

QString ImporterFileAmazonTransactions::getId() const
{
    return "AmazonTransactions";
}

QMap<QString, AbstractImporter::ParamInfo> ImporterFileAmazonTransactions::getRequiredParams() const
{
    return {};
}

QString ImporterFileAmazonTransactions::getUniqueReportId(const QString &filePath) const
{
    return QFileInfo(filePath).fileName();
}

bool ImporterFileAmazonTransactions::recomputeTaxes() const
{
    return true;
}

bool ImporterFileAmazonTransactions::isWrongIfConflict() const
{
    return true;
}

bool ImporterFileAmazonTransactions::fixRefundDate() const
{
    return true;
}

bool ImporterFileAmazonTransactions::isGroupedOrders() const
{
    return true;
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterFileAmazonTransactions::_loadReport(
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


    int indTransType = dataRode->header.pos("Transaction type");
    int indOrderId = dataRode->header.pos("Order ID");
    int indDate = dataRode->header.pos("Date");
    int indProductCharges = dataRode->header.pos("Total product charges");
    int indOther = dataRode->header.contains("Other") ? dataRode->header.pos("Other") : -1;
    
    // Dynamic currency detection
    QList<QString> currencies = {
        "EUR", "USD", "GBP", "CAD", "AUD", "JPY", "INR", "CNY", 
        "MXN", "BRL", "TRY", "AED", "SAR", "PLN", "SEK", "EGP", "SGD", "NZD"
    };
    QStringList totalCandidates;
    for (const auto &cur : currencies) {
        totalCandidates << QString("Total (%1)").arg(cur);
    }

    // This will throw CsvHeaderException if none are found, which matches the requirement
    int indTotal = dataRode->header.pos(totalCandidates);
    
    // Extract currency from the found header
    QString foundHeader = dataRode->header.getHeaderElements().at(indTotal);
    QString currency = foundHeader.mid(7, 3); // Extract XXX from Total (XXX)

    for (const auto &line : dataRode->lines) {
        QString transType = line.value(indTransType);
        
        // We only care about Refunds
        if (transType != "Refund") {
            continue;
        }

        QString orderId = line.value(indOrderId);
        if (orderId.isEmpty()) {
            continue;
        }

        // Parse Date
        QString dateStr = line.value(indDate);
        QDate date = parseDateFormats(dateStr, {
            "M/d/yyyy",
            "MM/dd/yyyy",
            "yyyy-MM-dd",
            "dd/MM/yyyy"
        });
        
        if (!date.isValid()) {
             qWarning() << "Invalid date format:" << dateStr << "in file" << filePath;
             continue;
        }

        if (result.orderInfos->dateMin.isNull() || date < result.orderInfos->dateMin) {
            result.orderInfos->dateMin = date;
        }
        if (result.orderInfos->dateMax.isNull() || date > result.orderInfos->dateMax) {
            result.orderInfos->dateMax = date;
        }

        // The refund amount is product charges + other (e.g. taxes/surcharges returned).
        double productCharges = line.value(indProductCharges).toDouble();
        double other = (indOther >= 0) ? line.value(indOther).toDouble() : 0.0;
        double refundAmount = productCharges + other;
        if (qFuzzyIsNull(refundAmount)) {
            continue;
        }
        result.orderInfos->orderId_refundClues[orderId].append({refundAmount, currency, date});
    }

    co_return result;
}
