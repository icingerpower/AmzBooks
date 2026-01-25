#include "ExceptionVatAccountExisting.h"
ExceptionVatAccountExisting::ExceptionVatAccountExisting(const QString &title, const QString &text)
    : std::runtime_error((title + ": " + text).toStdString())
{
}

QString ExceptionVatAccountExisting::errorTitle() const 
{ 
    return "Account Exists"; 
}

QString ExceptionVatAccountExisting::errorText() const 
{ 
    return what(); 
}
