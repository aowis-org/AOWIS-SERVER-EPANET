#ifndef AOWIS_EPANET_MULTI_QUALITY_RUN_EXECUTOR_H
#define AOWIS_EPANET_MULTI_QUALITY_RUN_EXECUTOR_H

#include <aowis/epanet/epanet_result_run.h>

#include <functional>
#include <memory>

#include <QString>

class EpanetPreparedProject;
class QTemporaryDir;

class EpanetMultiQualityRunExecutor
{
public:
    explicit EpanetMultiQualityRunExecutor(
        EpanetPreparedProject &prepared_project,
        bool persist_hydraulic_file = false);
    ~EpanetMultiQualityRunExecutor();

    bool hasHydraulicFile() const;
    QString hydraulicFilePath() const;
    QString hydraulicInpText() const;

    EpanetResultRun run(
        EpanetResultRun result,
        const std::function<bool()> &cancellation_requested = std::function<bool()>());

private:
    HydraulicSimulationStatus saveHydraulics();

    EpanetPreparedProject &prepared_project_;
    bool persist_hydraulic_file_ = false;
    std::unique_ptr<QTemporaryDir> hydraulic_artifact_directory_;
    QString hydraulic_file_path_;
    QString hydraulic_inp_text_;
};

#endif // AOWIS_EPANET_MULTI_QUALITY_RUN_EXECUTOR_H
