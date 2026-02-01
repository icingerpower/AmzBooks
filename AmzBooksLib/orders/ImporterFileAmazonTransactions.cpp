#include "ImporterFileAmazonTransactions.h"
#include "utils/CsvReader.h"
#include "utils/CsvHeader.h"
#include <QFileInfo>
#include <QDebug>
#include "SaleType.h"

DECLARE_IMPORTER_FILE(ImporterFileAmazonTransactions)

QString ImporterFileAmazonTransactions::getLabel() const
{
    return "Amazon Transactions Report";
}

ActivitySource ImporterFileAmazonTransactions::getActivitySource() const
{
    return {ActivitySourceType::Report, "Amazon", "Amazon Transactions", "Transactions Report"};
}

QMap<QString, AbstractImporter::ParamInfo> ImporterFileAmazonTransactions::getRequiredParams() const
{
    return {};
}

QString ImporterFileAmazonTransactions::getUniqueReportId(const QString &filePath) const
{
    return QFileInfo(filePath).fileName();
}

QCoro::Task<AbstractImporter::ReturnOrderInfos> ImporterFileAmazonTransactions::_loadReport(const QString &filePath)
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
    int indTotal = -1;
    QString currency;
    
    // Search for "Total (XXX)" column
    QStringList headers = dataRode->header.getHeaderElements();
    for (int i = 0; i < headers.size(); ++i) {
        QString header = headers.at(i);
        if (header.startsWith("Total (") && header.endsWith(")")) {
            indTotal = i;
            currency = header.mid(7, 3); // Extract XXX from Total (XXX)
            break;
        }
    }
    
    if (currency.isEmpty()) {
        CsvHeaderException ex;
        ex.setColumnValuesError({"Total (XXX)"});
        throw ex; // Currency column is mandatory
    }
    
    // If not found, maybe check "Total" and assume default or error?
    // The user provided sample has "Total (USD)".
    // Let's assume valid currency is found if Total (XXX) exists.
    
    // Extract country from filename
    // Format: YYYY-MM-transactions-XX.csv or similar
    QString fileName = QFileInfo(filePath).fileName();
    QString countryCode;
    
    QRegularExpression re("transactions-([a-z\\.]+)\\.csv");
    auto match = re.match(fileName);
    if (match.hasMatch()) {
        QString suffix = match.captured(1);
        if (suffix == "com") countryCode = "US";
        else if (suffix == "co.uk") countryCode = "GB";
        else countryCode = suffix.toUpper();
    } else {
         result.errorReturned = "Could not extract country from filename (expected format: ...transactions-XX.csv): " + fileName;
         co_return result;
    }

    for (const auto &line : dataRode->lines) {
        QString transType = line.value(indTransType);
        
        // We only care about Refunds as requested
        if (transType != "Refund") continue;

        QString orderId = line.value(indOrderId);
        if (orderId.isEmpty()) continue;

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

        if (result.orderInfos->dateMin.isNull() || date < result.orderInfos->dateMin) result.orderInfos->dateMin = date;
        if (result.orderInfos->dateMax.isNull() || date > result.orderInfos->dateMax) result.orderInfos->dateMax = date;

        // Amounts
        double productCharges = line.value(indProductCharges).toDouble();
        
        double amountTax = 0.0;
        double amountTotal = productCharges; 
        
        Amount amt(amountTotal, amountTax);
        
        auto actRes = Activity::create(
            orderId, // OrderID as EventID
            orderId, // ActivityID same as OrderID
            "", // subId
            date.startOfDay(),
            currency,
            countryCode, // depart (From)
            countryCode, // arrival (To) - Unknown so assume same country?
            "", // vatPaidTo
            amt,
            TaxSource::MarketplaceProvided, // Assumption
            "", // declaringCountry
            TaxScheme::Unknown,
            TaxJurisdictionLevel::Country,
            SaleType::Products
        );

        if (actRes.ok()) {
            QList<Activity> activities;
            activities.append(actRes.value.value());
            Refund refund(activities);
            result.orderInfos->refunds.append(refund);
            
            // Invoicing Info
            InvoicingInfo info(&result.orderInfos->refunds.last());
            result.orderInfos->invoicingInfos.append({orderId, info});
        } else {
             qWarning() << "Failed to create refund activity for order:" << orderId << "Error:" << (actRes.errors.isEmpty() ? "" : actRes.errors.first().message);
        }
    }

    co_return result;
}
