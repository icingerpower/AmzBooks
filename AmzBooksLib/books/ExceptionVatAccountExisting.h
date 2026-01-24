#ifndef BOOK_EXCEPTIONVATACCOUNTEXISTING_H
#define BOOK_EXCEPTIONVATACCOUNTEXISTING_H

#include <stdexcept>
#include <QString>

class ExceptionVatAccountExisting : public std::runtime_error
{
public:
    ExceptionVatAccountExisting(const QString &title, const QString &text)
        : std::runtime_error((title + ": " + text).toStdString())
    {}
    
    // Minimal API to satisfy existing calls if any
    QString errorTitle() const { return "Account Exists"; } 
    QString errorText() const { return what(); }
};

#endif // BOOK_EXCEPTIONVATACCOUNTEXISTING_H
