#include "BankStripe.h"
#include "utils/CsvReader.h"

BankStripe::BankStripe()
{
}

QStringList BankStripe::fileFilters() const
{
    return QStringList() << "stripe_*.csv";
}

QString BankStripe::defaultJournal() const
{
    return "STRIPE " + currency();
}

QSharedPointer<QList<AbstractBankStatement::BankRow>> BankStripe::readRows(const QString &filePath) const
{
    auto results = QSharedPointer<QList<BankRow>>::create();
    
    QString sep = ",";
    QString guill = "\"";
    CsvReader reader(filePath, sep, guill);
    reader.readAll();
    const DataFromCsv *dataRode = reader.dataRode();
    
    if (!dataRode) return results;

    int indDate = dataRode->header.pos({"created", "Created (UTC)", "Created date (UTC)"});
    int indName = dataRode->header.pos({"type", "Type", "Description"});
    int indComment = dataRode->header.pos("id");
    int indAmount = dataRode->header.pos({"Amount", "amount"});
    int indFees = dataRode->header.pos({"Fee", "fee"});
    int indCurrency = dataRode->header.pos({"Currency", "currency"});
    
    if (indDate == -1 || indAmount == -1 || indCurrency == -1) {
        return results;
    }

    for (const auto &elements : dataRode->lines) {
        QString rowCurrency = elements[indCurrency].toUpper();
        
        if (rowCurrency == this->currency()) {
            BankRow row;
            row.currency = this->currency();
            QString dateStr = elements[indDate].split(" ")[0]; 
            row.date = QDateTime::fromString(dateStr, "yyyy-MM-dd").date();
            
            if (!row.date.isValid()) continue;

            QString title;
            if (indName != -1) title += elements[indName];
            if (indComment != -1) {
                if (!title.isEmpty()) title += " ";
                title += elements[indComment];
            }
            row.label = title;
            
            QString amountStr = elements[indAmount];
            amountStr.replace(",", ".");
            row.amount = amountStr.toDouble();
            
            if (indFees != -1) {
                QString feesStr = elements[indFees];
                feesStr.replace(",", ".");
                row.fees = feesStr.toDouble(); 
            }
            
            if (qAbs(row.amount) > 0.001) {
                results->append(row);
            }
        }
    }
    
    return results;
}
