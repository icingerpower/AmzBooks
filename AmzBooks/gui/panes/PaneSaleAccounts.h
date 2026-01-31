#ifndef PANESALEACCOUNTS_H
#define PANESALEACCOUNTS_H

#include <QWidget>

namespace Ui {
class PaneSaleAccounts;
}

class PaneSaleAccounts : public QWidget
{
    Q_OBJECT

public:
    explicit PaneSaleAccounts(QWidget *parent = nullptr);
    ~PaneSaleAccounts();

public slots:
    void addRate();
    void removeRate();

private:
    Ui::PaneSaleAccounts *ui;
    void _connectSlots();
};

#endif // PANESALEACCOUNTS_H
