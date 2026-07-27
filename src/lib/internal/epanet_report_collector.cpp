#include "epanet_report_collector.h"

#include <QString>
#include <QtGlobal>

void EpanetReportCollector::clear()
{
    this->report_lines.clear();
}

QStringList EpanetReportCollector::lines() const
{
    return this->report_lines;
}

void EpanetReportCollector::callback(void *user_data, void *project_handle, const char *line)
{
    Q_UNUSED(project_handle)

    EpanetReportCollector *collector = static_cast<EpanetReportCollector *>(user_data);
    if (collector == nullptr || line == nullptr)
        return;

    collector->report_lines.append(QString::fromUtf8(line));
}
