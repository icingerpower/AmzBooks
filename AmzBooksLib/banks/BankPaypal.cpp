#include "BankPaypal.h"
#include "utils/CsvReader.h"
#include <QFileInfo>

BankPaypal::BankPaypal()
{
}

QStringList BankPaypal::fileFilters() const
{
    return QStringList() << "paypal_*.csv";
}

QString BankPaypal::defaultJournal() const
{
    return "BQ PAYPAL " + currency();
}

QSharedPointer<QList<AbstractBankStatement::BankRow>> BankPaypal::readRows(const QString &filePath) const
{
    auto results = QSharedPointer<QList<BankRow>>::create();
    
    QString sep = ",";
    QString guill = "\"";
    CsvReader reader(filePath, sep, guill);
    reader.readAll();
    const DataFromCsv *dataRode = reader.dataRode();
    
    if (!dataRode) return results;

    int indDate = dataRode->header.pos("Date");
    int indName = dataRode->header.pos({"Nom", "Name"});
    int indName2 = dataRode->header.pos({"Nom de la banque", "Bank name"});
    int indComment = dataRode->header.pos("Description");
    int indAmount = dataRode->header.pos("Brut");
    int indFees = dataRode->header.pos({"Frais", "Fees"});
    int indCurrency = dataRode->header.pos({"Devise", "Currency"});
    
    if (indDate == -1 || indAmount == -1 || indCurrency == -1) {
        return results;
    }

    for (const auto &elements : dataRode->lines) {
        QString rowCurrency = elements[indCurrency];
        if (rowCurrency == this->currency()) {
            BankRow row;
            row.currency = this->currency();
            row.date = QDateTime::fromString(elements[indDate], "dd/MM/yyyy").date();
            if (!row.date.isValid()) continue;

            QString title = elements[indComment];
            if (indName != -1 && !elements[indName].isEmpty()) {
                title = elements[indName] + " " + title;
            }
            if (indName2 != -1 && !elements[indName2].isEmpty()) {
                title = elements[indName2] + " " + title;
            }
            row.label = title;
            
            QString amountStr = elements[indAmount];
            amountStr.replace(",", ".");
            row.amount = amountStr.toDouble();
            
            if (indFees != -1) {
                QString feesStr = elements[indFees];
                feesStr.replace(",", ".");
                row.fees = -feesStr.toDouble(); 
            }
            
            if (qAbs(row.amount) > 0.001) {
                results->append(row);
            }
        }
    }
    
    return results;
}
