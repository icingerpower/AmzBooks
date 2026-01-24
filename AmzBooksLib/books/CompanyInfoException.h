#ifndef COMPANYINFOEXCEPTION_H
#define COMPANYINFOEXCEPTION_H

#include <QException>
#include <QString>

class CompanyInfoException : public QException
{
public:
    CompanyInfoException(const QString &title, const QString &text);
    void raise() const override;
    CompanyInfoException *clone() const override;
    
    const QString &errorTitle() const;
    const QString &errorText() const;

private:
    QString m_errorTitle;
    QString m_errorText;

};

#endif // COMPANYINFOEXCEPTION_H
