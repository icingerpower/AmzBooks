#ifndef BOOKSCONNECTIONS_H
#define BOOKSCONNECTIONS_H

#include <QDate>
#include <QDir>
#include <QString>
#include <QModelIndex>
#include <QMultiHash>
#include <QPair>

class AbstractBooksTable;
class AbstractBooksTableBank;
class EntrySelfTable;
class CurrencyRateManager;
class CompanyInfosTable;

class BooksConnections
{
public:
    BooksConnections(const QDir &workingDir);
    void tryToConnect(QHash<AbstractBooksTable *, QModelIndexList> &table_indexes
                      , CurrencyRateManager *currencyRateManager);
    void tryToConnect(AbstractBooksTableBank *left
                      , const QModelIndexList &indexesLeft
                      , EntrySelfTable *right
                      , const QModelIndex &indexRight);
    void disconnect(AbstractBooksTable *booksTable
                    , const QModelIndex &index);
    void disconnect(EntrySelfTable *selfTable
                    , const QModelIndex &index);
    bool contains(const QString &booksTableId, const QString &rowId) const;
    bool containsSelf(const QString &booksTableId, const QString &rowId) const;
    void associateTablesToIds(QList<AbstractBooksTable *> bookTables, const EntrySelfTable *selfEntryTable, const CompanyInfosTable *companyInfosTable = nullptr);
    QString getAccount2(AbstractBooksTableBank *tableBank, int row) const;
    // Returns the date of the linked non-bank entry, or an invalid QDate if linked to
    // another bank table or a self-entry. Used to pick the correct currency rate date
    // when a bank transaction settles one day after its paired book entry.
    QDate getLinkedDate(AbstractBooksTableBank *tableBank, int row) const;

private:
    QString m_filePathCsv;
    QMultiHash<QString, QString> m_id_id; // Contains in both way
    QString _getId(const QString &booksTableId, const QString &rowId) const;
    void _save();
    void _load();
    QHash<QString, QPair<const AbstractBooksTable *, int>> m_cacheId_table;
    QHash<QString, QPair<const EntrySelfTable *, int>> m_cacheId_tableSelf;
    QString m_internalBankAccount;
};

#endif // BOOKSCONNECTIONS_H
