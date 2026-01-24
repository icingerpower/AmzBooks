#include "CompanyInfoException.h"

CompanyInfoException::CompanyInfoException(const QString &title, const QString &text)
    : m_errorTitle(title), m_errorText(text)
{
}

void CompanyInfoException::raise() const
{
    throw *this;
}

CompanyInfoException *CompanyInfoException::clone() const
{
    return new CompanyInfoException(*this);
}

const QString &CompanyInfoException::errorTitle() const
{
    return m_errorTitle;
}

const QString &CompanyInfoException::errorText() const
{
    return m_errorText;
}
