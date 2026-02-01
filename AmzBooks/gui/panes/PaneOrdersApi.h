#ifndef PANEORDERSAPI_H
#define PANEORDERSAPI_H

#include <QWidget>

namespace Ui {
class PaneOrdersApi;
}

class PaneOrdersApi : public QWidget
{
    Q_OBJECT

public:
    explicit PaneOrdersApi(QWidget *parent = nullptr);
    ~PaneOrdersApi();

private:
    Ui::PaneOrdersApi *ui;
};

#endif // PANEORDERSAPI_H
