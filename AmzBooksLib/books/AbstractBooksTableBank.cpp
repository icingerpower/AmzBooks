#include "banks/AbstractBankStatement.h"

#include "AbstractBooksTableBank.h"

AbstractBooksTableBank::AbstractBooksTableBank(const BooksConnections *bookConnections, const QDir &workingDir, QObject *parent)
    : AbstractBooksTable(bookConnections, workingDir, parent)
{
}

void AbstractBooksTableBank::addFilePaths(const QStringList &bankFilePaths)
{
    auto bankStatement = getBankStatement();
    for (const auto &filePath : bankFilePaths)
    {
        auto bankRows = bankStatement->readRows(filePath);
        for (const auto &bankRow : *bankRows)
        {
            const auto &rowId = bankRow.date.toString("yyyyMMdd")
                    + "_" + bankRow.label
                    + "_" + QString::number(bankRow.amount, 'f', 2);
            
            if (qAbs(bankRow.amount) > 0)
            {
                add(rowId,
                    bankRow.date,
                    bankRow.amount,
                    bankRow.currency,
                    bankRow.label,
                    bankStatement->defaultAccount(),
                    "",
                    0.0,
                    "",
                    "");
            }
            if (qAbs(bankRow.fees) > 0)
            {
                add(rowId,
                    bankRow.date,
                    bankRow.amount,
                    bankRow.currency,
                    bankRow.label,
                    bankStatement->defaultAccount(),
                    "",
                    0.0,
                    "",
                    "");
            }
        }
    }
}

QString AbstractBooksTableBank::getId() const
{
    return getBankStatement()->getId();
}

