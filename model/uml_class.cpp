
#include "uml_class.h"

UmlClass::UmlClass(const std::string& name)
    : m_name(name)
{
}

const std::string& UmlClass::name() const
{
    return m_name;
}

const std::vector<std::string>& UmlClass::attributes() const
{
    return m_attributes;
}

const std::vector<std::string>& UmlClass::methods() const
{
    return m_methods;
}

void UmlClass::addAttribute(const std::string& attribute)
{
    addAttribute(attribute, UmlAccess::Unknown);
}

void UmlClass::addMethod(const std::string& method)
{
    addMethod(method, UmlAccess::Unknown);
}

void UmlClass::addAttribute(
    const std::string& attribute,
    UmlAccess access)
{
    m_attributes.push_back(attribute);
    m_attributeMembers.emplace_back(attribute, access);
}

void UmlClass::addMethod(
    const std::string& method,
    UmlAccess access)
{
    m_methods.push_back(method);
    m_methodMembers.emplace_back(method, access);
}

const std::vector<UmlMember>& UmlClass::attributeMembers() const
{
    return m_attributeMembers;
}

const std::vector<UmlMember>& UmlClass::methodMembers() const
{
    return m_methodMembers;
}
