#include "uml_relationship.h"

UmlRelationship::UmlRelationship(
    const std::string& source,
    const std::string& target,
    RelationshipType type,
    const std::string& sourceMultiplicity,
    const std::string& targetMultiplicity
    )
    : m_source(source)
    , m_target(target)
    , m_type(type)
    , m_sourceMultiplicity(sourceMultiplicity)
    , m_targetMultiplicity(targetMultiplicity)
{
}

const std::string& UmlRelationship::source() const
{
    return m_source;
}

const std::string& UmlRelationship::target() const
{
    return m_target;
}

RelationshipType UmlRelationship::type() const
{
    return m_type;
}

const std::string& UmlRelationship::sourceMultiplicity() const
{
    return m_sourceMultiplicity;
}

const std::string& UmlRelationship::targetMultiplicity() const
{
    return m_targetMultiplicity;
}