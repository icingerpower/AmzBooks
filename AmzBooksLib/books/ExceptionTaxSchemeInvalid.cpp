#include "ExceptionTaxSchemeInvalid.h"

ExceptionTaxSchemeInvalid::ExceptionTaxSchemeInvalid(const QString &title, const QString &text)
    : m_errorTitle(title), m_errorText(text)
{
}

void ExceptionTaxSchemeInvalid::raise() const
{
    throw *this;
}

ExceptionTaxSchemeInvalid *ExceptionTaxSchemeInvalid::clone() const
{
    return new ExceptionTaxSchemeInvalid(*this);
}


const QString &ExceptionTaxSchemeInvalid::errorTitle() const
{
    return m_errorTitle;
}

const QString &ExceptionTaxSchemeInvalid::errorText() const
{
    return m_errorText;
}
