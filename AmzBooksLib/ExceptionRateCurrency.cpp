#include "ExceptionRateCurrency.h"

ExceptionRateCurrency::ExceptionRateCurrency(const QString &url)
    : m_url(url)
{
}

void ExceptionRateCurrency::raise() const
{
    throw *this;
}

ExceptionRateCurrency *ExceptionRateCurrency::clone() const
{
    return new ExceptionRateCurrency(*this);
}

const QString &ExceptionRateCurrency::url() const
{
    return m_url;
}

void ExceptionRateCurrency::setUrl(const QString &url)
{
    m_url = url;
}
