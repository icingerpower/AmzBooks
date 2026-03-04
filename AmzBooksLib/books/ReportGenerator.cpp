#include "ReportGenerator.h"

#include <stdexcept>
#include <QTextDocument>
#include <QPdfWriter>

ReportGenerator::ReportGenerator()
    : m_inTable(false)
    , m_landscape(false)
    , m_currentTableColumnsCount(0)
    , m_pageSize(QPageSize::A4)
    , m_fontScale(1.0)
{
    // The <style> block is the only reliable way to apply padding to table
    // cells in Qt's rich text renderer (inline CSS padding is not supported).
    m_html += "<html><head><style>th,td{padding:4px;}</style></head><body>\n";
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

    m_html += "<table border=\"1\" style=\"white-space:nowrap\">\n";
    m_html += "  <tr>\n";
    for (const QString &label : headerLabels) {
        m_html += "    <th style=\"white-space:nowrap\">" + label + "</th>\n";
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
        m_html += "    <td style=\"white-space:nowrap\">" + elem + "</td>\n";
    }
    m_html += "  </tr>\n";
}

void ReportGenerator::addRowTotal(const QStringList &rowElements)
{
    if (!m_inTable) {
        throw std::runtime_error("No table started.");
    }
    if (rowElements.size() != m_currentTableColumnsCount) {
        throw std::runtime_error("Invalid number of elements for the current table row.");
    }

    m_html += "  <tr style=\"background-color:#dde8f0; font-weight:bold; border-top:2px solid #333;\">\n";
    for (const QString &elem : rowElements) {
        m_html += "    <td style=\"white-space:nowrap\"><b>" + elem + "</b></td>\n";
    }
    m_html += "  </tr>\n";
}

void ReportGenerator::endTable()
{
    if (!m_inTable) {
        throw std::runtime_error("No table started.");
    }

    m_html += "</table>\n<br><br>\n";
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

void ReportGenerator::setPageSize(QPageSize::PageSizeId pageSize)
{
    m_pageSize = pageSize;
}

void ReportGenerator::setFontScale(double scale)
{
    m_fontScale = scale;
}

void ReportGenerator::save(const QString &pdfFilePath) const
{
    auto applyFontScale = [this](QTextDocument &doc) {
        if (m_fontScale != 1.0) {
            QFont f = doc.defaultFont();
            f.setPointSizeF(qMax(1.0, f.pointSizeF() * m_fontScale));
            doc.setDefaultFont(f);
        }
    };

    const QSizeF baseSizeMm = QPageSize(m_pageSize).size(QPageSize::Millimeter);
    const double pageHeightMm = m_landscape
        ? qMin(baseSizeMm.width(), baseSizeMm.height())
        : baseSizeMm.height();
    const double pageWidthMm = (m_landscape
        ? qMax(baseSizeMm.width(), baseSizeMm.height())
        : baseSizeMm.width()) * 1.8;

    QPdfWriter writer(pdfFilePath);
    writer.setPageSize(QPageSize(QSizeF(pageWidthMm, pageHeightMm), QPageSize::Millimeter));
    writer.setResolution(300);
    writer.setPageMargins(QMarginsF(3.0, 3.0, 3.0, 3.0));

    QTextDocument document;
    applyFontScale(document);
    document.setHtml(getHtml());
    document.print(&writer);
}
