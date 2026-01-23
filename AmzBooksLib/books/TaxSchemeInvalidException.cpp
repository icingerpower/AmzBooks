#include "TaxSchemeInvalidException.h"

TaxSchemeInvalidException::TaxSchemeInvalidException(const QString &title, const QString &text)
    : m_errorTitle(title), m_errorText(text)
{
}

void TaxSchemeInvalidException::raise() const
{
    throw *this;
}

TaxSchemeInvalidException *TaxSchemeInvalidException::clone() const
{
    return new TaxSchemeInvalidException(*this);
}


const QString &TaxSchemeInvalidException::errorTitle() const
{
    return m_errorTitle;
}

const QString &TaxSchemeInvalidException::errorText() const
{
    return m_errorText;
}
