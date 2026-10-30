#pragma once

#include "../model/uml_diagram.h"
#include <string>

class CppParser
{
public:
    bool parseFile(
        const std::string& filePath,
        UmlDiagram& diagram,
        std::string& errorMessage
        );
};
