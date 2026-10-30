
#pragma once

#include <string>
#include <vector>

// Модификатор доступа члена класса.
enum class UmlAccess
{
    Public,
    Protected,
    Private,
    Unknown
};

// Элемент UML-класса: атрибут или метод.
struct UmlMember
{
    std::string text;
    UmlAccess access = UmlAccess::Unknown;

    UmlMember() = default;

    explicit UmlMember(
        const std::string& memberText,
        UmlAccess memberAccess = UmlAccess::Unknown
        )
        : text(memberText),
        access(memberAccess)
    {
    }
};

class UmlClass
{
public:
    explicit UmlClass(const std::string& name);

    const std::string& name() const;

    // Старые методы оставляем для совместимости
    // с текущим парсером и отображением.
    const std::vector<std::string>& attributes() const;
    const std::vector<std::string>& methods() const;

    void addAttribute(const std::string& attribute);
    void addMethod(const std::string& method);

    // Новые методы с указанием модификатора доступа.
    void addAttribute(
        const std::string& attribute,
        UmlAccess access
        );

    void addMethod(
        const std::string& method,
        UmlAccess access
        );

    const std::vector<UmlMember>& attributeMembers() const;
    const std::vector<UmlMember>& methodMembers() const;

private:
    std::string m_name;

    // Старые списки для совместимости.
    std::vector<std::string> m_attributes;
    std::vector<std::string> m_methods;

    // Новые списки с модификаторами доступа.
    std::vector<UmlMember> m_attributeMembers;
    std::vector<UmlMember> m_methodMembers;
};
