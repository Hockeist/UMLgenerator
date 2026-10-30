
#include "uml_diagram.h"

void UmlDiagram::addClass(const UmlClass& umlClass)
{
    // Не добавляем класс повторно.
    if (findClass(umlClass.name()))
        return;

    m_classes.push_back(umlClass);
}

const std::vector<UmlClass>& UmlDiagram::classes() const
{
    return m_classes;
}

const UmlClass* UmlDiagram::findClass(
    const std::string& name) const
{
    for (const UmlClass& umlClass : m_classes)
    {
        if (umlClass.name() == name)
            return &umlClass;
    }

    return nullptr;
}

UmlClass* UmlDiagram::findClassMutable(
    const std::string& name)
{
    for (UmlClass& umlClass : m_classes)
    {
        if (umlClass.name() == name)
            return &umlClass;
    }

    return nullptr;
}

void UmlDiagram::addRelationship(
    const UmlRelationship& relationship)
{
    // Не добавляем одинаковую связь повторно.
    for (const UmlRelationship& existing : m_relationships)
    {
        if (existing.source() == relationship.source() &&
            existing.target() == relationship.target() &&
            existing.type() == relationship.type())
        {
            return;
        }
    }

    m_relationships.push_back(relationship);
}

const std::vector<UmlRelationship>& UmlDiagram::relationships() const
{
    return m_relationships;
}
