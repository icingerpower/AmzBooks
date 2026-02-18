#include "ExceptionWithTitleText.h"

ExceptionWithTitleText::ExceptionWithTitleText(const QString &title, const QString &text)
    : m_errorTitle(title), m_errorText(text)
{
}

void ExceptionWithTitleText::raise() const
{
    throw *this;
}

ExceptionWithTitleText *ExceptionWithTitleText::clone() const
{
    return new ExceptionWithTitleText(*this);
}

const char *ExceptionWithTitleText::what() const noexcept
{
    if (m_whatMsg.isEmpty()) {
        QString msg = m_errorTitle;
        if (!m_errorText.isEmpty()) {
            msg += ": " + m_errorText;
        }
        m_whatMsg = msg.toUtf8();
    }
    return m_whatMsg.constData();
}
