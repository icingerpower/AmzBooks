#ifndef ABSTRACTBOOKSAVER_H
#define ABSTRACTBOOKSAVER_H

#include <QDir>
#include <QHash>
#include <QMultiMap>
#include <QDate>
#include <QSharedPointer>
#include <QObject>
#include <functional>
#include <QMap>

class AbstractBooksTable;
class JournalEntry;

class AbstractBookSaver
{
public:
    AbstractBookSaver();
    virtual ~AbstractBookSaver() = default;

    virtual QString getId() const = 0;
    
    // The main public save method called by the user
    // It delegates to the protected pure virtual save(...) which might be different?
    // User requested: "void save(const QList<AbstractBooksTable *> &tables, const QDir &workingDir, const QDir &outDir);"
    // But the user *also* removed that method in the diff.
    // And requested: "Same in <year_dir>/<month_dir>/all/all_<year_dir>_<month_dir>.csv"
    // The user diff *replaced* `save(tables...)` with the protected `save(entries...)`.
    // Wait, if I am to implement `BookSaverFull` inheriting `AbstractBookSaver`, `BookSaverFull` needs to receive the entries.
    // So `AbstractBookSaver` probably needs to be the one PRODUCING the entries, OR `AbstractBookSaver` is just the writer.
    
    // User said: "I removed things in AbstractBookSaver. Just add the Recorder pattern ... Then create a class BookSaverFull ... Save data in the csv format ..."
    // The user's diff shows `AbstractBookSaver` ONLY has the `save` method that takes `entries`.
    // SO: The logic to produce `entries` from `tables` is gone from `AbstractBookSaver`. 
    // Is it expected to be put back? 
    // "create a class BookSaverFull ... - Save data in the csv format" implies BookSaverFull implements the writing logic.
    // Who calls `save`? 
    // Maybe `AbstractBookSaver` is just an interface for "Saving Entries".
    
    // I will stick to what is in the file (plus Recorder) as requested.
    
    using FactoryFunc = std::function<AbstractBookSaver*()>;
    static const QMap<QString, FactoryFunc> &ALL_SAVERS();

    class Recorder {
    public:
        Recorder(const QString& id, FactoryFunc factory);
    };

    virtual void save(
            const QHash<QString, QMultiMap<QDate, QSharedPointer<JournalEntry>>> &journal_date_entries
            , const QDir &outDir) = 0;

protected:
    static QMap<QString, FactoryFunc> &getSavers();
    
};

#define DECLARE_BOOK_SAVER(NEW_CLASS) \
    AbstractBookSaver *create##NEW_CLASS() { \
        return new NEW_CLASS(); \
    } \
    NEW_CLASS prototype##NEW_CLASS; \
    AbstractBookSaver::Recorder recorder##NEW_CLASS{prototype##NEW_CLASS.getId(), create##NEW_CLASS};

#endif // ABSTRACTBOOKSAVER_H
