#ifndef EXCEPTIONWITHTITLETEXT_H
#define EXCEPTIONWITHTITLETEXT_H

#include <QException>
#include <QString>

class ExceptionWithTitleText : public QException
{
public:
    ExceptionWithTitleText(const QString &title, const QString &text);
    
    void raise() const override;
    ExceptionWithTitleText *clone() const override;
    
    // std::exception compatibility: returns combination of title and text
    const char* what() const noexcept override;
    
    const QString &errorTitle() const { return m_errorTitle; }
    const QString &errorText() const { return m_errorText; }

private:
    QString m_errorTitle;
    QString m_errorText;
    mutable QByteArray m_whatMsg;
};

#endif // EXCEPTIONWITHTITLETEXT_H
