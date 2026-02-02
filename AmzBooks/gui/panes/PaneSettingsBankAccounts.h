#ifndef PANESETTINGSBANKACCOUNTS_H
#define PANESETTINGSBANKACCOUNTS_H

#include <QWidget>

namespace Ui {
class PaneSettingsBankAccounts;
}

class PaneSettingsBankAccounts : public QWidget
{
    Q_OBJECT

public:
    explicit PaneSettingsBankAccounts(QWidget *parent = nullptr);
    ~PaneSettingsBankAccounts();

private:
    Ui::PaneSettingsBankAccounts *ui;
};

#endif // PANESETTINGSBANKACCOUNTS_H
