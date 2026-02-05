#ifndef BOOKSAVERFULL_H
#define BOOKSAVERFULL_H

#include "AbstractBookSaver.h"

class BookSaverFull : public AbstractBookSaver
{
public:
    BookSaverFull();
    virtual ~BookSaverFull() override = default;

    QString getId() const override { return "BookSaverFull"; }

    void save(
            const QHash<QString, QMultiMap<QDate, QSharedPointer<JournalEntry>>> &journal_date_entries
            , const QDir &outDir) override;
};


#endif // BOOKSAVERFULL_H
