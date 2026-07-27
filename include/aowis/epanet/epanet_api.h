#ifndef AOWIS_EPANET_API_H
#define AOWIS_EPANET_API_H

#if __has_include(<epanet2_2.h>)
#include <epanet2_2.h>
#elif __has_include(<epanet2.h>)
#include <epanet2.h>
#else
#error "Could not find EPANET header."
#endif

#endif
