#ifndef ABSTRACTBOOKSTABLEBANK_H
#define ABSTRACTBOOKSTABLEBANK_H

#include "AbstractBooksTable.h"

class AbstractBankStatement;

class AbstractBooksTableBank : public AbstractBooksTable
{
    Q_OBJECT

public:
    explicit AbstractBooksTableBank(const BooksConnections *bookConnections, const QDir &workingDir, QObject *parent = nullptr);

    void addFilePaths(const QStringList &bankFilePaths);

    QString getId() const override;

    virtual const AbstractBankStatement *getBankStatement() const = 0;

};

#endif // ABSTRACTBOOKSTABLEBANK_H
