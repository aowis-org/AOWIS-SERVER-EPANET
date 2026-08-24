#ifndef AOWIS_EPANET_INP_ROUNDTRIP_PROOF_H
#define AOWIS_EPANET_INP_ROUNDTRIP_PROOF_H

#include "conformance_test_framework.h"

#include <QString>

namespace AowisEpanetTests
{
void proveInpRoundTrip(TestContext &context, const QString &source_file);
}

#endif // AOWIS_EPANET_INP_ROUNDTRIP_PROOF_H
