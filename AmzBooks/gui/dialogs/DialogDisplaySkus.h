#ifndef DIALOGDISPLAYSKUS_H
#define DIALOGDISPLAYSKUS_H

#include <QDialog>
#include <QStringList>

namespace Ui {
class DialogDisplaySkus;
}

class DialogDisplaySkus : public QDialog
{
    Q_OBJECT

public:
    explicit DialogDisplaySkus(const QStringList &skus, QWidget *parent = nullptr);
    ~DialogDisplaySkus();

private slots:
    void onExport();

private:
    Ui::DialogDisplaySkus *ui;
};

#endif // DIALOGDISPLAYSKUS_H
