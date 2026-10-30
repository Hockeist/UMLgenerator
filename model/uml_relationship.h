#pragma once

#include <string>

enum class RelationshipType
{
    Inheritance,
    Association,
    Aggregation,
    Composition,
    Dependency
};

class UmlRelationship
{
public:
    // Конструктор принимает исходный класс, целевой класс, тип связи
    // и (опционально) кратность на обоих концах линии, например "1" и "0..*"
    UmlRelationship(
        const std::string& source,
        const std::string& target,
        RelationshipType type,
        const std::string& sourceMultiplicity = "",
        const std::string& targetMultiplicity = ""
        );

    const std::string& source() const;
    const std::string& target() const;
    RelationshipType type() const;

    const std::string& sourceMultiplicity() const;
    const std::string& targetMultiplicity() const;

private:
    std::string m_source;
    std::string m_target;
    RelationshipType m_type;
    std::string m_sourceMultiplicity;
    std::string m_targetMultiplicity;
};