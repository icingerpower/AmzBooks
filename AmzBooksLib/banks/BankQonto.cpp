#include "BankQonto.h"
#include "utils/CsvReader.h"

BankQonto::BankQonto()
{
}

QString BankQonto::getId() const
{
    return "qonto";
}

QString BankQonto::getName() const
{
    return "Qonto";
}

QStringList BankQonto::fileFilters() const
{
    return QStringList() << "qonto_*.csv";
}

QString BankQonto::defaultAccount() const
{
    return "512501";
}

QString BankQonto::defaultAccountFees() const
{
    return "627340";
}

QString BankQonto::defaultJournal() const
{
    return "BQ QONTO";
}

QSharedPointer<QList<AbstractBankStatement::BankRow>> BankQonto::readRows(const QString &filePath) const
{
    auto results = QSharedPointer<QList<BankRow>>::create();
    
    QString sep = ",";
    QString guill = "\"";
    CsvReader reader(filePath, sep, guill);
    reader.readAll();
    const DataFromCsv *dataRode = reader.dataRode();
    
    if (!dataRode) return results;

    // "value_date_local", "operation_date_local", "Operation date (UTC)"
    int indDate = dataRode->header.pos({"value_date_local", "operation_date_local", "Operation date (UTC)"});
    // "settlement_date_local", "Settlement date (local)"
    int indDate2 = dataRode->header.pos({"settlement_date_local", "Settlement date (local)"});
    // "comment", "Reference"
    int indName = dataRode->header.pos({"comment", "Reference"});
    // "counterpart_name", "Counterparty name"
    int indComment = dataRode->header.pos({"counterpart_name", "Counterparty name"});
    // "amount", "Total amount (incl. VAT)"
    int indAmount = dataRode->header.pos({"amount", "Total amount (incl. VAT)"});
    // "local_amount", "Total amount (incl. VAT) (local)"
    int indAmountLocal = dataRode->header.pos({"local_amount", "Total amount (incl. VAT) (local)"});
    // "local_amount_currency", "Currency"
    int indCurrencyLocal = dataRode->header.pos({"local_amount_currency", "Currency"});
    
    if ((indDate == -1 && indDate2 == -1) || indAmount == -1) {
        return results;
    }

    for (const auto &elements : dataRode->lines) {
        BankRow row;
        QDate date;
        // Logic from old code: try date2, else date1. Format "dd-MM-yyyy hh:mm:ss"
        if (indDate2 != -1 && !elements[indDate2].isEmpty()) {
             date = QDateTime::fromString(elements[indDate2], "dd-MM-yyyy hh:mm:ss").date();
        } else if (indDate != -1) {
             date = QDateTime::fromString(elements[indDate], "dd-MM-yyyy hh:mm:ss").date();
        }
        
        if (!date.isValid()) continue; // Q_ASSERT(date.isValid()) in old code means it expects validity
        row.date = date;
        row.currency = "EUR";

        QString title;
        if (indName != -1) title += elements[indName];
        if (indComment != -1) {
            if (!title.isEmpty()) title += " ";
            title += elements[indComment];
        }
        title.replace("'", ""); // Old code did this
        
        QString origCurrency;
        if (indCurrencyLocal != -1) origCurrency = elements[indCurrencyLocal];
        
        if (origCurrency != "EUR" && !origCurrency.isEmpty()) {
            if (indAmountLocal != -1) {
                title = elements[indAmountLocal] + " " + origCurrency + " " + title;
            }
        }
        row.label = title;
        
        row.amount = elements[indAmount].toDouble();
        row.fees = 0.;
        
        if (qAbs(row.amount) > 0.001) {
            results->append(row);
        }
    }
    
    return results;
}

DECLARE_BANK_STATEMENT(BankQonto)
