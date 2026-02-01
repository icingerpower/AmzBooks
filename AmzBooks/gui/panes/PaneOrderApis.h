#ifndef PANEORDERAPIS_H
#define PANEORDERAPIS_H

#include <QWidget>
#include <QMap>
#include <QDate>

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>

namespace Ui {
class PaneOrderApis;
}

class PaneOrderApis : public QWidget
{
    Q_OBJECT

public:
    explicit PaneOrderApis(QWidget *parent = nullptr);
    ~PaneOrderApis();

public slots:
    void import(); 

private:
    Ui::PaneOrderApis *ui;
    class ApiImportersTable *m_importersTable;
    class ParamsTable *m_paramsModel = nullptr;
    QMap<QString, class ActivityTable*> m_activityModels;

    void _connectSlots();
    void onImporterSelected(const QModelIndex &current, const QModelIndex &previous);
    void updateChart();

    QMap<QString, QMap<QDate, int>> m_ordersData;
};

#endif // PANEORDERAPIS_H
