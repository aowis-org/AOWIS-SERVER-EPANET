#pragma once

#include <QString>

class SimulationMessage;

class EpanetError
{
public:
    static QString message(int code);
};
