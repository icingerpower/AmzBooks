#ifndef EXCEPTIONACCOUNTMISSING_H
#define EXCEPTIONACCOUNTMISSING_H

#include <QException>
#include <QString>

class ExceptionAccountMissing : public QException
{
public:
    explicit ExceptionAccountMissing(const QString &amazonSite);
    void raise() const override;
    ExceptionAccountMissing *clone() const override;
    QString amazonSite() const;
    const char *what() const noexcept override;

private:
    QString m_amazonSite;
    mutable QByteArray m_msg;
};

#endif // EXCEPTIONACCOUNTMISSING_H
