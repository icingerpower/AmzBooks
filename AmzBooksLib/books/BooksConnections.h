#ifndef BOOKSCONNECTIONS_H
#define BOOKSCONNECTIONS_H

#include <QDir>
#include <QString>
#include <QModelIndex>

class AbstractBooksTable;
class AbstractBooksTableBank;
class EntrySelfTable;

class BooksConnections
{
public:
    BooksConnections(const QDir &workingDir);
    void tryToConnect(AbstractBooksTable *left
                      , const QModelIndex &indexLeft
                      , AbstractBooksTableBank *right
                      , const QModelIndex &indexRight
                      , double currencyRate = 1.0);
    void tryToConnect(AbstractBooksTable *left
                      , const QModelIndex &indexLeft
                      , EntrySelfTable *right
                      , const QModelIndex &indexRight);
    void disconnect(AbstractBooksTable *booksTable
                    , const QModelIndex &index);
    bool contains(const QString &booksTableId, const QString &rowId) const;

private:
    QString m_filePathCsv;
    QHash<QString, QString> m_id_id; // Contains in both way
    QString _getId(const QString &booksTableId, const QString &rowId) const;
    void _save();
    void _load();
};

#endif // BOOKSCONNECTIONS_H
