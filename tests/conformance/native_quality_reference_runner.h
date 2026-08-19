#ifndef AOWIS_EPANET_NATIVE_QUALITY_REFERENCE_RUNNER_H
#define AOWIS_EPANET_NATIVE_QUALITY_REFERENCE_RUNNER_H

#include <aowis/model/hydraulic/network_hydraulic.h>

#include <QHash>
#include <QList>
#include <QString>

#include <cstdint>

namespace AowisEpanetTests
{
struct NativeQualityReferenceStep
{
    std::int64_t time_s = 0;
    QHash<QString, double> node_quality;
    QHash<QString, double> node_source_mass_mg_per_min;
    QHash<QString, double> link_quality;
    double mass_balance_ratio = 0.0;
};

struct NativeQualityReferenceTimeline
{
    bool success = false;
    QString error;
    QList<NativeQualityReferenceStep> results;
};

NativeQualityReferenceTimeline runNativeQualityReference(const QString &input_file, const NetworkHydraulic &network);
}

#endif // AOWIS_EPANET_NATIVE_QUALITY_REFERENCE_RUNNER_H
