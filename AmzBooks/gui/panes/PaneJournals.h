#ifndef PANEJOURNALS_H
#define PANEJOURNALS_H

#include <QWidget>

namespace Ui {
class PaneJournals;
}

class PaneJournals : public QWidget
{
    Q_OBJECT

public:
    explicit PaneJournals(QWidget *parent = nullptr);
    ~PaneJournals();

private:
    Ui::PaneJournals *ui;
};

#endif // PANEJOURNALS_H
