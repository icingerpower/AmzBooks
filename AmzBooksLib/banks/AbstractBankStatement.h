#ifndef ABSTRACTBANKSTATEMENT_H
#define ABSTRACTBANKSTATEMENT_H

#include <QMap>
#include <QString>
#include <QDate>
#include <QObject>

class AbstractBankStatement : public QObject
{
    Q_OBJECT
public:
    struct BankRow{
        QDate date;
        QString label;
        double amount = 0.;
        double fees = 0.;
        QString currency;
    };
    static const QMap<QString, AbstractBankStatement *> &ALL_BANKS();

    AbstractBankStatement(QObject *parent = nullptr);
    virtual ~AbstractBankStatement() override = default;
    virtual QString getId() const = 0;
    virtual QString getName() const = 0;
    virtual QStringList fileFilters() const;
    virtual QString defaultAccount() const = 0;
    virtual QString defaultAccountFees() const = 0;
    virtual QString defaultJournal() const; // Default BQ (Bank Q)
    virtual QSharedPointer<QList<BankRow>> readRows(const QString &filePath) const = 0;

    static QMap<QString, AbstractBankStatement *> &getBanks();

    class Recorder{
    public:
        Recorder(const QString& id, AbstractBankStatement* statement);
    };
protected:
};

#define DECLARE_BANK_STATEMENT(NEW_CLASS) \
    NEW_CLASS prototype##NEW_CLASS; \
    AbstractBankStatement::Recorder recorder##NEW_CLASS{prototype##NEW_CLASS.getId(), &prototype##NEW_CLASS};

#endif // ABSTRACTBANKSTATEMENT_H
