#ifndef AOWIS_EPANET_RUNNER_H
#define AOWIS_EPANET_RUNNER_H

#include <aowis/epanet/epanet_result_import.h>
#include <aowis/epanet/epanet_result_inp.h>
#include <aowis/epanet/epanet_result_run.h>
#include <aowis/epanet/epanet_run_request.h>

#include <functional>

#include <QString>

class EpanetRunner
{
public:
    EpanetResultImport importInp(const QString &input_file_path) const;

    EpanetResultInp retrieveInp(const EpanetRunRequest &request) const;

    EpanetResultRun run(const EpanetRunRequest &request) const;
    EpanetResultRun run(const EpanetRunRequest &request, const std::function<bool()> &cancellation_requested) const;
};

#endif
