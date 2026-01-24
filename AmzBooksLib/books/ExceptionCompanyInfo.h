#ifndef BOOK_EXCEPTIONCOMPANYINFO_H
#define BOOK_EXCEPTIONCOMPANYINFO_H

#include <QException>
#include <QString>

class ExceptionCompanyInfo : public QException
{
public:
    ExceptionCompanyInfo(const QString &title, const QString &text);
    void raise() const override;
    ExceptionCompanyInfo *clone() const override;
    
    const QString &errorTitle() const;
    const QString &errorText() const;

private:
    QString m_errorTitle;
    QString m_errorText;

};

#endif // BOOK_EXCEPTIONCOMPANYINFO_H
