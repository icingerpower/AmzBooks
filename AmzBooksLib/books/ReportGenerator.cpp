#include "ReportGenerator.h"

#include <stdexcept>
#include <QTextDocument>
#include <QPdfWriter>

ReportGenerator::ReportGenerator()
    : m_inTable(false)
    , m_landscape(false)
    , m_currentTableColumnsCount(0)
{
    m_html += "<html><body>\n";
}

void ReportGenerator::addTitle(const QString &title)
{
    m_html += "<h1>" + title + "</h1><br>\n";
}

void ReportGenerator::addSubtitle(const QString &subtitle)
{
    m_html += "<h2>" + subtitle + "</h2><br>\n";
}

void ReportGenerator::startTable(const QStringList &headerLabels)
{
    if (m_inTable) {
        throw std::runtime_error("A table is already started.");
    }

    m_inTable = true;
    m_currentTableColumnsCount = headerLabels.size();

    m_html += "<table border=\"1\">\n";
    m_html += "  <tr>\n";
    for (const QString &label : headerLabels) {
        m_html += "    <th>" + label + "</th>\n";
    }
    m_html += "  </tr>\n";
}

void ReportGenerator::addRow(const QStringList &rowElements)
{
    if (!m_inTable) {
        throw std::runtime_error("No table started.");
    }
    if (rowElements.size() != m_currentTableColumnsCount) {
        throw std::runtime_error("Invalid number of elements for the current table row.");
    }

    m_html += "  <tr>\n";
    for (const QString &elem : rowElements) {
        m_html += "    <td>" + elem + "</td>\n";
    }
    m_html += "  </tr>\n";
}

void ReportGenerator::endTable()
{
    if (!m_inTable) {
        throw std::runtime_error("No table started.");
    }

    m_html += "</table>\n";
    m_inTable = false;
    m_currentTableColumnsCount = 0;
}

QString ReportGenerator::getHtml() const
{
    QString finalHtml = m_html;
    if (m_inTable) {
        finalHtml += "</table>\n";
    }
    finalHtml += "</body></html>\n";
    return finalHtml;
}

void ReportGenerator::setLandscape(bool landscape)
{
    m_landscape = landscape;
}

void ReportGenerator::save(const QString &pdfFilePath) const
{
    QTextDocument document;
    document.setHtml(getHtml());

    QPdfWriter writer(pdfFilePath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    if (m_landscape) {
        writer.setPageOrientation(QPageLayout::Landscape);
    }
    writer.setResolution(300); // High res for text
    writer.setPageMargins(QMarginsF(3.0, 3.0, 3.0, 3.0)); // mm

    document.print(&writer);
}
