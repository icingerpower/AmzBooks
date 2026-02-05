#ifndef BOOKSCONNECTIONS_H
#define BOOKSCONNECTIONS_H

#include <QDir>
#include <QString>
#include <QModelIndex>
#include <QMultiHash>

class AbstractBooksTable;
class AbstractBooksTableBank;
class EntrySelfTable;
class CurrencyRateManager;

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
    bool contains(const QString &booksTableId, const QString &rowId) const;
    void associateTablesToIds(QList<const AbstractBooksTable *> bookTables, const EntrySelfTable *selfEntryTable);
    QString getAccount2(AbstractBooksTableBank *tableBank, int row) const;

private:
    QString m_filePathCsv;
    QMultiHash<QString, QString> m_id_id; // Contains in both way
    QString _getId(const QString &booksTableId, const QString &rowId) const;
    void _save();
    void _load();
    QHash<QString, const AbstractBooksTable *> m_cacheId_table;
    QHash<QString, const EntrySelfTable *> m_cacheId_tableSelf;
};

#endif // BOOKSCONNECTIONS_H
