#ifndef AOWIS_EPANET_REPORT_COLLECTOR_H
#define AOWIS_EPANET_REPORT_COLLECTOR_H

#include <QStringList>

class EpanetReportCollector
{
public:
    void captureCurrentLinesAsHeader();
    void clear();
    QStringList headerLines() const;
    QStringList lines() const;

    static void callback(void *user_data, void *project_handle, const char *line);

private:
    QStringList report_header_lines;
    QStringList report_lines;
};

#endif
