#ifndef EXCEPTIONBOOKEQUALITY_H
#define EXCEPTIONBOOKEQUALITY_H

#include <exception>
#include <QString>
#include <string>

class ExceptionBookEquality : public std::exception
{
public:
    ExceptionBookEquality(const QString &message);
    virtual const char* what() const noexcept override;

private:
    std::string m_message;
};

#endif // EXCEPTIONBOOKEQUALITY_H
