#ifndef VATACCOUNTEXISTINGEXCEPTION_H
#define VATACCOUNTEXISTINGEXCEPTION_H

#include <stdexcept>
#include <QString>

class VatAccountExistingException : public std::runtime_error
{
public:
    VatAccountExistingException(const QString &title, const QString &text)
        : std::runtime_error((title + ": " + text).toStdString())
    {}
    
    // Minimal API to satisfy existing calls if any
    QString errorTitle() const { return "Account Exists"; } 
    QString errorText() const { return what(); }
};

#endif // VATACCOUNTEXISTINGEXCEPTION_H
