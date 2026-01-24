#ifndef BOOK_EXCEPTIONFILEERROR_H
#define BOOK_EXCEPTIONFILEERROR_H

#include <QException>
#include <QString>

class ExceptionFileError : public QException
{
public:
    ExceptionFileError(const QString &title, const QString &text);
    void raise() const override;
    ExceptionFileError *clone() const override;
    
    const QString &errorTitle() const;
    const QString &errorText() const;

private:
    QString m_errorTitle;
    QString m_errorText;

};

#endif // BOOK_EXCEPTIONFILEERROR_H
