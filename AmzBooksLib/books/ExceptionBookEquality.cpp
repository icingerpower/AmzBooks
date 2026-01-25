#include "ExceptionBookEquality.h"
ExceptionBookEquality::ExceptionBookEquality(const QString &message) 
    : m_message(message.toStdString()) 
{
}

const char* ExceptionBookEquality::what() const noexcept 
{
    return m_message.c_str();
}
