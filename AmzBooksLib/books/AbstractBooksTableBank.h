#ifndef ABSTRACTBOOKSTABLEBANK_H
#define ABSTRACTBOOKSTABLEBANK_H

#include "AbstractBooksTable.h"
#include <QMap>

class AbstractBankStatement;

class AbstractBooksTableBank : public AbstractBooksTable
{
    Q_OBJECT
public:
    AbstractBooksTableBank(const BooksConnections *bookConnections, const QDir &workingDir, QObject *parent = nullptr);
    virtual ~AbstractBooksTableBank() override = default;

    virtual const AbstractBankStatement *getBankStatement() const = 0;

    void addFilePaths(const QStringList &filePaths, bool saveToDisk = true);
    void load(int year);
    void removeFile(const QString &filePath);
    void remove(const QList<QModelIndex> &indices);

    using FactoryFunc = std::function<AbstractBooksTableBank*(const BooksConnections*, const QDir&, QObject*)>;
    static const QMap<QString, FactoryFunc> &ALL_TABLES();

    class Recorder {
    public:
        Recorder(const QString& id, FactoryFunc factory);
    };

protected:
    static QMap<QString, FactoryFunc> &getTables();
    QString _recordFilePaths(int year, const QString &filePath);
    QStringList _loadFilePaths(int year);
    
    QHash<QString, QString> m_rowId_filePath;
    QDir m_workingDir;
};

#define DECLARE_BOOKS_TABLE_BANK(NEW_CLASS) \
    AbstractBooksTableBank *create##NEW_CLASS(const BooksConnections *bc, const QDir &wd, QObject *p) { \
        return new NEW_CLASS(bc, wd, p); \
    } \
    NEW_CLASS prototype##NEW_CLASS{nullptr, QDir(), nullptr}; \
    AbstractBooksTableBank::Recorder recorder##NEW_CLASS{prototype##NEW_CLASS.getId(), create##NEW_CLASS};

#endif // ABSTRACTBOOKSTABLEBANK_H
