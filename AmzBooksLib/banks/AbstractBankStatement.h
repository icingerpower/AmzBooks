#ifndef ABSTRACTBANKSTATEMENT_H
#define ABSTRACTBANKSTATEMENT_H

#include <QMap>
#include <QString>
#include <QDate>

class AbstractBankStatement
{
public:
    struct BankRow{
        QDate date;
        QString label;
        double amount = 0.;
        double fees = 0.;
    };
    static const QMap<QString, const AbstractBankStatement *> &ALL_BANKS();
    AbstractBankStatement();
    virtual ~AbstractBankStatement() = default;
    virtual QString getId() const = 0;
    virtual QString getName() const = 0;
    virtual QStringList fileFilters() const;
    virtual QString defaultAccount() const = 0;
    virtual QString defaultAccountFees() const = 0;
    virtual QString defaultJournal() const = 0;
    virtual QSharedPointer<QList<BankRow>> readRows(const QString &filePath) const = 0;

    class Recorder{
    public:
        Recorder(const AbstractBankStatement *dataGetter);
    };
protected:
    static QMap<QString, const AbstractBankStatement *> _BANKS;
};

#define DECLARE_CLASS(NEW_CLASS) \
NEW_CLASS instance##NEW_CLASS; \
    NEW_CLASS::Recorder recorder##NEW_CLASS{&instance##NEW_CLASS};

#endif // ABSTRACTBANKSTATEMENT_H
