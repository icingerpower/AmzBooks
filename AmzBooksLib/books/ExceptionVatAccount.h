#ifndef EXCEPTIONVATACCOUNT_H
#define EXCEPTIONVATACCOUNT_H

#include <QException>
#include <QString>

class ExceptionVatAccount : public QException
{
public:
    ExceptionVatAccount(const QString &title, const QString &text);

    void raise() const override;
    ExceptionVatAccount *clone() const override;

    QString title() const { return m_title; }
    QString text() const { return m_text; }
    
    // std::exception compatibility
    const char* what() const noexcept override;

private:
    QString m_title;
    QString m_text;
    QByteArray m_msg;
};

#endif // EXCEPTIONVATACCOUNT_H
