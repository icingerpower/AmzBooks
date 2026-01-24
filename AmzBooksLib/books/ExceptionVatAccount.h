#ifndef EXCEPTIONVATACCOUNT_H
#define EXCEPTIONVATACCOUNT_H

#include <QException>
#include <QString>

class ExceptionVatAccount : public QException
{
public:
    ExceptionVatAccount(const QString &title, const QString &text)
        : m_title(title), m_text(text)
    {}

    void raise() const override { throw *this; }
    ExceptionVatAccount *clone() const override { return new ExceptionVatAccount(*this); }

    QString title() const { return m_title; }
    QString text() const { return m_text; }
    
    // std::exception compatibility
    const char* what() const noexcept override {
        return m_msg.data();
    }

private:
    QString m_title;
    QString m_text;
    QByteArray m_msg = (m_title + ": " + m_text).toUtf8();
};

#endif // EXCEPTIONVATACCOUNT_H
