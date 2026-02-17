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

    // Required columns
    QStringList requiredColumns = {
        "Transaction type",
        "Order ID",
        "Date",
        "Total product charges",
    };

    for (const QString &col : requiredColumns) {
         if (!dataRode->header.contains(col)) {
             CsvHeaderException ex;
             ex.setColumnValuesError({col});
             throw ex;
         }
    }

    int indTransType = dataRode->header.pos("Transaction type");
    int indOrderId = dataRode->header.pos("Order ID");
    int indDate = dataRode->header.pos("Date");
    int indProductCharges = dataRode->header.pos("Total product charges");
    
    // Dynamic currency detection
    QString currency;
    
    // Search for "Total (XXX)" column
    QStringList headers = dataRode->header.getHeaderElements();
    for (int i = 0; i < headers.size(); ++i) {
        QString header = headers.at(i);
        if (header.startsWith("Total (") && header.endsWith(")")) {
            currency = header.mid(7, 3); // Extract XXX from Total (XXX)
            break;
        }
    }
    
    if (currency.isEmpty()) {
        CsvHeaderException ex;
        ex.setColumnValuesError({"Total (XXX)"});
        throw ex; // Currency column is mandatory
    }

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

        // Store refund clue: orderId -> {amount, currency}
        double productCharges = line.value(indProductCharges).toDouble();
        result.orderInfos->orderId_refundClue[orderId] = {productCharges, currency};
    }

    co_return result;
}
