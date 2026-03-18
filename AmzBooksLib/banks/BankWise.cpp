#include "BankWise.h"
#include "utils/CsvReader.h"
#include <QFile>
#include <QTextStream>

BankWise::BankWise()
{
}

QString BankWise::hasWarnings(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString{};
    }
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.contains("wise charges", Qt::CaseInsensitive)) {
            return QString{};
        }
    }
    return tr("This file does not contain any \"Wise Charges\" rows. "
              "It may be a simplified export without detailed fee breakdown.\n\n"
              "Do you still want to import this file?");
}

QStringList BankWise::fileFilters() const
{
    return QStringList() << "transferwise_*.csv";
}

QString BankWise::defaultJournal() const
{
    return "BQ WISE " + currency();
}

QSharedPointer<QList<AbstractBankStatement::BankRow>> BankWise::readRows(const QString &filePath) const
{
    auto results = QSharedPointer<QList<BankRow>>::create();
    
    QString sep = ",";
    QString guill = "\"";
    CsvReader reader(filePath, sep, guill);
    reader.readAll();
    const DataFromCsv *dataRode = reader.dataRode();
    
    if (!dataRode) return results;

    int indDate = dataRode->header.pos("Date");
    int indName = dataRode->header.pos("Payer Name");
    int indName2 = dataRode->header.pos("Payee Name");
    int indName3 = dataRode->header.pos("Merchant");
    int indComment = dataRode->header.pos("Description");
    int indAmount = dataRode->header.pos("Amount");
    int indCurrency = dataRode->header.pos("Currency");
    
    if (indDate == -1 || indAmount == -1 || indCurrency == -1) {
        return results;
    }

    for (const auto &elements : dataRode->lines) {
        if (elements[indCurrency] == this->currency()) {
            BankRow row;
            row.currency = this->currency();
            row.date = QDateTime::fromString(elements[indDate], "dd-MM-yyyy").date();
            
            if (!row.date.isValid()) continue;

            QString title = elements[indComment];
            if (indName != -1 && !elements[indName].isEmpty()) {
                title = elements[indName] + " " + title;
            }
            if (indName2 != -1 && !elements[indName2].isEmpty()) {
                title = elements[indName2] + " " + title;
            }
            if (indName3 != -1 && !elements[indName3].isEmpty()) {
                title = elements[indName3] + " " + title;
            }
            row.label = title;
            row.amount = elements[indAmount].toDouble();
            row.fees = 0.;
            
            if (qAbs(row.amount) > 0.001) {
                results->append(row);
            }
        }
    }
    
    return results;
}
