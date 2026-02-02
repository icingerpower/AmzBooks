#include "ExceptionAccountMissing.h"

ExceptionAccountMissing::ExceptionAccountMissing(const QString &amazonSite)
    : m_amazonSite(amazonSite)
{
}

void ExceptionAccountMissing::raise() const
{
    throw *this;
}

ExceptionAccountMissing *ExceptionAccountMissing::clone() const
{
    return new ExceptionAccountMissing(*this);
}

QString ExceptionAccountMissing::amazonSite() const
{
    return m_amazonSite;
}

const char *ExceptionAccountMissing::what() const noexcept
{
    if (m_msg.isEmpty()) {
        m_msg = QString("Amazon site '%1' is missing an associated account.").arg(m_amazonSite).toUtf8();
    }
    return m_msg.constData();
}
