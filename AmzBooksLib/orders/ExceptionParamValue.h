#ifndef EXCEPTIONPARAMVALUE_H
#define EXCEPTIONPARAMVALUE_H

#include <QException>
#include <QString>

class ExceptionParamValue : public QException
{
public:
    ExceptionParamValue(const QString &title, const QString &text);

    void raise() const override;
    ExceptionParamValue *clone() const override;
    
    const QString &errorTitle() const { return m_errorTitle; }
    const QString &errorText() const { return m_errorText; }

private:
    QString m_errorTitle;
    QString m_errorText;
};

#endif // EXCEPTIONPARAMVALUE_H
