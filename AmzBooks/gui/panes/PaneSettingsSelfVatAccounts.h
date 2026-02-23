#ifndef PANESETTINGSSELFVATACCOUNTS_H
#define PANESETTINGSSELFVATACCOUNTS_H

#include <QWidget>

namespace Ui {
class PaneSettingsSelfVatAccounts;
}

class PaneSettingsSelfVatAccounts : public QWidget
{
    Q_OBJECT

public:
    explicit PaneSettingsSelfVatAccounts(QWidget *parent = nullptr);
    ~PaneSettingsSelfVatAccounts();

private:
    Ui::PaneSettingsSelfVatAccounts *ui;
};

#endif // PANESETTINGSSELFVATACCOUNTS_H
