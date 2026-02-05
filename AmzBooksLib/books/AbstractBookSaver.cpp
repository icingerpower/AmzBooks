#include "AbstractBookSaver.h"
#include "AbstractBooksTable.h"
#include "JournalEntry.h"

AbstractBookSaver::AbstractBookSaver()
{
}

const QMap<QString, AbstractBookSaver::FactoryFunc> &AbstractBookSaver::ALL_SAVERS()
{
    return getSavers();
}

QMap<QString, AbstractBookSaver::FactoryFunc> &AbstractBookSaver::getSavers()
{
    static QMap<QString, FactoryFunc> savers;
    return savers;
}

AbstractBookSaver::Recorder::Recorder(const QString& id, FactoryFunc factory)
{
    AbstractBookSaver::getSavers().insert(id, factory);
}
