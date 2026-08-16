#ifndef AOWIS_EPANET_HYDRAULIC_RESULT_COMPARATOR_H
#define AOWIS_EPANET_HYDRAULIC_RESULT_COMPARATOR_H

#include "conformance_test_framework.h"
#include "native_epanet_reference_runner.h"

#include <aowis/epanet/epanet_result_run.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

namespace AowisEpanetTests
{
void compareHydraulicTimelines(const NativeHydraulicTimeline &expected,
    const EpanetResultRun &actual,
    const NetworkHydraulic &network,
    TestContext &context);
}

#endif // AOWIS_EPANET_HYDRAULIC_RESULT_COMPARATOR_H
