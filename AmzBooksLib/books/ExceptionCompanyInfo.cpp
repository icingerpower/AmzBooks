#include "ExceptionCompanyInfo.h"

ExceptionCompanyInfo::ExceptionCompanyInfo(const QString &title, const QString &text)
    : m_errorTitle(title), m_errorText(text)
{
}

void ExceptionCompanyInfo::raise() const
{
    throw *this;
}

ExceptionCompanyInfo *ExceptionCompanyInfo::clone() const
{
    return new ExceptionCompanyInfo(*this);
}

const QString &ExceptionCompanyInfo::errorTitle() const
{
    return m_errorTitle;
}

const QString &ExceptionCompanyInfo::errorText() const
{
    return m_errorText;
}
