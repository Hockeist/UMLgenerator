#pragma once

#include "uml_class.h"
#include "uml_relationship.h"

#include <string>
#include <vector>

class UmlDiagram
{
public:
    void addClass(const UmlClass& umlClass);

    const std::vector<UmlClass>& classes() const;

    const UmlClass* findClass(const std::string& name) const;

    UmlClass* findClassMutable(const std::string& name);

    void addRelationship(const UmlRelationship& relationship);

    const std::vector<UmlRelationship>& relationships() const;

private:
    std::vector<UmlClass> m_classes;
    std::vector<UmlRelationship> m_relationships;
};
