#include "VoxelForge/Core/Version.h"

#include <sstream>

namespace VoxelForge::Core
{
    std::string Version::ToString() const
    {
        std::ostringstream stream;
        stream << major << '.' << minor << '.' << patch;
        return stream.str();
    }
}
