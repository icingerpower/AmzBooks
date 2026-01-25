#ifndef BOOK_EXCEPTIONVATACCOUNTEXISTING_H
#define BOOK_EXCEPTIONVATACCOUNTEXISTING_H

#include <stdexcept>
#include <QString>

class ExceptionVatAccountExisting : public std::runtime_error
{
public:
    ExceptionVatAccountExisting(const QString &title, const QString &text);
    
    // Minimal API to satisfy existing calls if any
    QString errorTitle() const;
    QString errorText() const;
};

#endif // BOOK_EXCEPTIONVATACCOUNTEXISTING_H
