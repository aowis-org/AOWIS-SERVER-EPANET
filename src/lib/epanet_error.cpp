#include "epanet_error.h"

#if __has_include(<epanet2_2.h>)
#include <epanet2_2.h>
#elif __has_include(<epanet2.h>)
#include <epanet2.h>
#else
#error "Could not find EPANET header."
#endif

#include <array>

QString EpanetError::message(int code)
{
    if (code == 0)
        return QString();
    
    std::array<char, 512> buffer{};
    EN_geterror(code, buffer.data(), static_cast<int>(buffer.size()));
    
    QString message = QString::fromUtf8(buffer.data()).trimmed();
    
    if (message.isEmpty())
        message = QStringLiteral("Unknown EPANET error.");
    
    return message;
}
