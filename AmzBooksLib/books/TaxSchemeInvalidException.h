#ifndef TAXSCHEMEINVALIDEXCEPTION_H
#define TAXSCHEMEINVALIDEXCEPTION_H

#include <QException>
#include <QString>

class TaxSchemeInvalidException : public QException
{
public:
    TaxSchemeInvalidException(const QString &title, const QString &text);
    void raise() const override;
    TaxSchemeInvalidException *clone() const override;
    
    const QString &errorTitle() const;
    const QString &errorText() const;

private:
    QString m_errorTitle;
    QString m_errorText;

};

#endif // TAXSCHEMEINVALIDEXCEPTION_H
