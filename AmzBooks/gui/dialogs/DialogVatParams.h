#ifndef DIALOGVATPARAMS_H
#define DIALOGVATPARAMS_H

#include <QDialog>

namespace Ui {
class DialogVatParams;
}

class DialogVatParams : public QDialog
{
    Q_OBJECT

public:
    explicit DialogVatParams(const QString &errorTitle, 
                             const QString &errorText, 
                             QWidget *parent = nullptr);
    ~DialogVatParams();

private:
    Ui::DialogVatParams *ui;
};

#endif // DIALOGVATPARAMS_H
