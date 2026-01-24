#include "ExceptionFileError.h"

ExceptionFileError::ExceptionFileError(const QString &title, const QString &text)
    : m_errorTitle(title), m_errorText(text)
{
}

void ExceptionFileError::raise() const
{
    throw *this;
}

ExceptionFileError *ExceptionFileError::clone() const
{
    return new ExceptionFileError(*this);
}

const QString &ExceptionFileError::errorTitle() const
{
    return m_errorTitle;
}

const QString &ExceptionFileError::errorText() const
{
    return m_errorText;
}
