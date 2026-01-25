#include "orders/ExceptionParamValue.h"

ExceptionParamValue::ExceptionParamValue(const QString &title, const QString &text)
    : m_errorTitle(title), m_errorText(text)
{
}

void ExceptionParamValue::raise() const
{
    throw *this;
}

ExceptionParamValue *ExceptionParamValue::clone() const
{
    return new ExceptionParamValue(*this);
}
