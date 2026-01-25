#include "ExceptionVatAccount.h"

ExceptionVatAccount::ExceptionVatAccount(const QString &title, const QString &text)
    : m_title(title), m_text(text)
{
    m_msg = (m_title + ": " + m_text).toUtf8();
}

void ExceptionVatAccount::raise() const
{
    throw *this;
}

ExceptionVatAccount *ExceptionVatAccount::clone() const
{
    return new ExceptionVatAccount(*this);
}

const char* ExceptionVatAccount::what() const noexcept
{
    return m_msg.data();
}
