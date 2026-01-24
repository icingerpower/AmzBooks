#ifndef EXCEPTIONBOOKEQUALITY_H
#define EXCEPTIONBOOKEQUALITY_H

#include <exception>
#include <QString>
#include <string>

class ExceptionBookEquality : public std::exception
{
public:
    ExceptionBookEquality(const QString &message) : m_message(message.toStdString()) {}
    virtual const char* what() const noexcept override {
        return m_message.c_str();
    }

private:
    std::string m_message;
};

#endif // EXCEPTIONBOOKEQUALITY_H
