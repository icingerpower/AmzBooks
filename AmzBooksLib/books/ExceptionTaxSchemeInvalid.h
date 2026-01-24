#ifndef BOOK_EXCEPTIONTAXSCHEMEINVALID_H
#define BOOK_EXCEPTIONTAXSCHEMEINVALID_H

#include <QException>
#include <QString>

class ExceptionTaxSchemeInvalid : public QException
{
public:
    ExceptionTaxSchemeInvalid(const QString &title, const QString &text);
    void raise() const override;
    ExceptionTaxSchemeInvalid *clone() const override;
    
    const QString &errorTitle() const;
    const QString &errorText() const;

private:
    QString m_errorTitle;
    QString m_errorText;

};

#endif // BOOK_EXCEPTIONTAXSCHEMEINVALID_H
