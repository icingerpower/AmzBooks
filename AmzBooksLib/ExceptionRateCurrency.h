#ifndef MODEL_EXCEPTIONRATECURRENCY_H
#define MODEL_EXCEPTIONRATECURRENCY_H

#include <QException>
#include <QString>

class ExceptionRateCurrency : public QException
{
public:
    ExceptionRateCurrency(const QString &url);
    void raise() const override;
    ExceptionRateCurrency *clone() const override;
    
    const QString &url() const;
    void setUrl(const QString &url);

private:
    QString m_url;
};

#endif // MODEL_EXCEPTIONRATECURRENCY_H
